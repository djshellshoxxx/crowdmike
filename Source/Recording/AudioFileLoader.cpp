#include "AudioFileLoader.h"
#include <cmath>

namespace crowdmike::rec {

namespace {
// RBJ biquad low-pass (direct form I), used only offline.
struct Lowpass {
    double b0 {}, b1 {}, b2 {}, a1 {}, a2 {};
    Lowpass(double sr, double hz, double q) {
        const double w = 2.0 * juce::MathConstants<double>::pi * hz / sr;
        const double alpha = std::sin(w) / (2.0 * q), c = std::cos(w), a0 = 1.0 + alpha;
        b0 = (1.0 - c) * 0.5 / a0; b1 = (1.0 - c) / a0; b2 = b0; a1 = -2.0 * c / a0; a2 = (1.0 - alpha) / a0;
    }
    void run(float* x, int n, bool backwards) const {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (int k = 0; k < n; ++k) {
            const int i = backwards ? n - 1 - k : k;
            const double in = x[i];
            const double y = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = in; y2 = y1; y1 = y;
            x[i] = (float) y;
        }
    }
};
} // namespace

void AudioFileLoader::resample(const juce::AudioBuffer<float>& in, double inRate, double outRate,
                               juce::AudioBuffer<float>& out) {
    const int nch = in.getNumChannels(), inLen = in.getNumSamples();
    if (inRate <= 0 || outRate <= 0 || std::abs(inRate - outRate) < 1.0e-9 || inLen == 0) {
        out.makeCopyOf(in);
        return;
    }
    const auto outLen64 = (juce::int64) std::llround((double) inLen * outRate / inRate);
    const int outLen = (int) juce::jmin<juce::int64>(outLen64, std::numeric_limits<int>::max() - 1);
    out.setSize(nch, outLen, false, false, false);
    const double ratio = inRate / outRate;

    juce::AudioBuffer<float> filtered;
    const juce::AudioBuffer<float>* src = &in;
    if (outRate < inRate) {   // anti-alias: 8th-order zero-phase Butterworth (2 sections, forward + backward)
        filtered.makeCopyOf(in);
        const double fc = 0.45 * outRate;
        const Lowpass s1(inRate, fc, 0.5411961), s2(inRate, fc, 1.3065630);
        for (int c = 0; c < nch; ++c) {
            auto* d = filtered.getWritePointer(c);
            s1.run(d, inLen, false); s2.run(d, inLen, false);
            s1.run(d, inLen, true);  s2.run(d, inLen, true);
        }
        src = &filtered;
    }
    for (int c = 0; c < nch; ++c) {
        const float* x = src->getReadPointer(c);
        float* y = out.getWritePointer(c);
        auto at = [&](juce::int64 i) -> double { return (i < 0 || i >= inLen) ? 0.0 : (double) x[i]; };
        for (int n = 0; n < outLen; ++n) {
            const double t = (double) n * ratio;
            const auto i = (juce::int64) std::floor(t);
            const double f = t - (double) i;
            if (f < 1.0e-12) { y[n] = (float) at(i); continue; }
            const double xm1 = at(i - 1), x0 = at(i), x1 = at(i + 1), x2 = at(i + 2);
            // 4-point, 3rd-order Lagrange
            const double c0 = -f * (f - 1.0) * (f - 2.0) / 6.0;
            const double c1 = (f + 1.0) * (f - 1.0) * (f - 2.0) / 2.0;
            const double c2 = -(f + 1.0) * f * (f - 2.0) / 2.0;
            const double c3 = (f + 1.0) * f * (f - 1.0) / 6.0;
            y[n] = (float) (c0 * xm1 + c1 * x0 + c2 * x1 + c3 * x2);
        }
    }
}

bool AudioFileLoader::load(const juce::File& file, double targetSampleRate, int maxChannels, double maxSeconds,
                           juce::int64 maxBytes, Result& out, juce::String& error, const std::atomic<bool>* cancel) {
    auto cancelled = [cancel] { return cancel != nullptr && cancel->load(std::memory_order_relaxed); };
    if (! file.existsAsFile()) { error = "File not found: " + file.getFullPathName(); return false; }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (reader == nullptr) { error = "Unsupported or corrupt audio file: " + file.getFileName(); return false; }

    const double sr = reader->sampleRate;
    const auto len = reader->lengthInSamples;
    if (! (sr >= 1000.0 && sr <= 1.0e6) || reader->numChannels < 1 || len <= 0) {
        error = "Invalid audio file (sample rate/length): " + file.getFileName();
        return false;
    }
    const double seconds = (double) len / sr;
    if (maxSeconds > 0.0 && seconds > maxSeconds) {
        error = "File too long (" + juce::String(seconds / 60.0, 1) + " min, limit " + juce::String(maxSeconds / 60.0, 0)
                + " min): " + file.getFileName();
        return false;
    }
    const int nch = juce::jlimit(1, juce::jlimit(1, 64, maxChannels), (int) reader->numChannels);
    const double outFrames = seconds * targetSampleRate;
    const auto bytes = (juce::int64) ((double) nch * juce::jmax((double) len, outFrames) * sizeof(float) * 2.0);
    if (maxBytes > 0 && bytes > maxBytes) { error = "Not enough memory budget to load " + file.getFileName(); return false; }
    if (len > (juce::int64) std::numeric_limits<int>::max() / 2) { error = "File too large: " + file.getFileName(); return false; }

    juce::AudioBuffer<float> raw;
    try {
        raw.setSize(nch, (int) len);
    } catch (const std::bad_alloc&) {
        error = "Out of memory loading " + file.getFileName();
        return false;
    }
    constexpr int chunk = 1 << 16;
    for (juce::int64 pos = 0; pos < len; pos += chunk) {
        if (cancelled()) { error = "Cancelled"; return false; }
        const int n = (int) juce::jmin<juce::int64>(chunk, len - pos);
        float* dst[64] {};
        for (int c = 0; c < nch; ++c) dst[c] = raw.getWritePointer(c, (int) pos);
        if (! reader->read(dst, nch, pos, n)) { error = "Read error (corrupt file?): " + file.getFileName(); return false; }
    }
    // Sanitise: non-finite samples (corrupt float files) become 0.
    for (int c = 0; c < nch; ++c) {
        auto* d = raw.getWritePointer(c);
        for (int i = 0; i < (int) len; ++i)
            if (! std::isfinite(d[i])) d[i] = 0.0f;
    }
    if (cancelled()) { error = "Cancelled"; return false; }

    out.sourceSampleRate = sr;
    out.sourceChannels = (int) reader->numChannels;
    out.sourceFrames = len;
    if (std::abs(sr - targetSampleRate) < 1.0e-6 || targetSampleRate <= 0) out.audio = std::move(raw);
    else resample(raw, sr, targetSampleRate, out.audio);
    return true;
}

} // namespace crowdmike::rec
