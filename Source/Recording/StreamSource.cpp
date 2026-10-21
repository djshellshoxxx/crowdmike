#include "StreamSource.h"
#include "AudioFileLoader.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace crowdmike::rec {

namespace {
constexpr int kAntiAliasGuard = 2048;   // frames of settling margin for the zero-phase filter at chunk edges
}

bool StreamSource::open(const juce::File& file, double engineRate, juce::String& error) {
    reader.reset();
    if (! file.existsAsFile()) { error = "File not found: " + file.getFullPathName(); return false; }
    fm.registerBasicFormats();
    reader.reset(fm.createReaderFor(file));
    if (reader == nullptr) { error = "Unsupported or corrupt audio file: " + file.getFileName(); return false; }
    const double sr = reader->sampleRate;
    if (! (sr >= 1000.0 && sr <= 1.0e6) || reader->numChannels < 1 || reader->lengthInSamples <= 0 || engineRate <= 0.0) {
        reader.reset();
        error = "Invalid audio file (sample rate/length): " + file.getFileName();
        return false;
    }
    inf = {};
    inf.sourceRate = sr;
    inf.engineRate = engineRate;
    inf.sourceChannels = (int) reader->numChannels;
    inf.channels = reader->numChannels >= 2 ? 2 : 1;
    inf.sourceFrames = reader->lengthInSamples;
    inf.frames = std::abs(sr - engineRate) < 1.0e-6 ? inf.sourceFrames
                                                     : (juce::int64) std::llround((double) inf.sourceFrames * engineRate / sr);
    if (inf.frames < 1) { reader.reset(); error = "File too short: " + file.getFileName(); return false; }
    return true;
}

bool StreamSource::readRaw(juce::int64 start, int n, float* const* dst, int numDst) {
    // zero-padded read of source frames [start, start + n)
    for (int c = 0; c < numDst; ++c) std::fill(dst[c], dst[c] + n, 0.0f);
    const juce::int64 a = std::max<juce::int64>(start, 0), b = std::min<juce::int64>(start + n, inf.sourceFrames);
    if (b <= a) return true;
    const int nch = inf.channels;
    float* d[2] = { dst[0] + (a - start), numDst > 1 ? dst[1] + (a - start) : nullptr };
    bool ok = true;
    if (! reader->read(d, nch, a, (int) (b - a))) {
        ok = false;
        for (int c = 0; c < nch; ++c) std::fill(d[c], d[c] + (b - a), 0.0f);
    }
    for (int c = 0; c < nch; ++c)
        for (juce::int64 i = 0; i < b - a; ++i)
            if (! std::isfinite(d[c][i])) d[c][i] = 0.0f;
    if (nch == 1 && numDst > 1) std::memcpy(d[1], d[0], sizeof(float) * (size_t) (b - a));
    return ok;
}

bool StreamSource::read(juce::int64 start, int n, float* L, float* R) {
    if (n <= 0) return true;
    if (reader == nullptr) { std::fill(L, L + n, 0.0f); std::fill(R, R + n, 0.0f); return false; }
    // portion inside [0, frames)
    const juce::int64 a = std::max<juce::int64>(start, 0), b = std::min<juce::int64>(start + n, inf.frames);
    std::fill(L, L + n, 0.0f);
    std::fill(R, R + n, 0.0f);
    if (b <= a) return true;
    const int cnt = (int) (b - a);
    float* oL = L + (a - start);
    float* oR = R + (a - start);

    if (std::abs(inf.sourceRate - inf.engineRate) < 1.0e-6) {
        float* d[2] = { oL, oR };
        const bool ok = readRaw(a, cnt, d, 2);
        if (! ok) ++failedReads;
        return ok;
    }

    const double ratio = inf.sourceRate / inf.engineRate;
    const bool down = inf.engineRate < inf.sourceRate;
    const int guard = down ? kAntiAliasGuard : 0;
    const juce::int64 lo = (juce::int64) std::floor((double) a * ratio) - 1 - guard;
    const juce::int64 hi = (juce::int64) std::floor((double) (b - 1) * ratio) + 2 + guard;
    const int count = (int) (hi - lo + 1);
    for (auto& v : raw) if ((int) v.size() < count) v.resize((size_t) count);
    float* rp[2] = { raw[0].data(), raw[1].data() };
    const bool ok = readRaw(lo, count, rp, 2);
    if (down)
        for (int c = 0; c < inf.channels; ++c) AudioFileLoader::antiAlias(rp[c], count, inf.sourceRate, inf.engineRate);
    if (inf.channels == 1) std::memcpy(rp[1], rp[0], sizeof(float) * (size_t) count);
    for (int k = 0; k < cnt; ++k) {
        const double t = (double) (a + k) * ratio;
        const auto i = (juce::int64) std::floor(t);
        const double f = t - (double) i;
        const int ix = (int) (i - lo);
        if (f < 1.0e-12) { oL[k] = rp[0][ix]; oR[k] = rp[1][ix]; continue; }
        oL[k] = AudioFileLoader::lagrange4(rp[0][ix - 1], rp[0][ix], rp[0][ix + 1], rp[0][ix + 2], f);
        oR[k] = AudioFileLoader::lagrange4(rp[1][ix - 1], rp[1][ix], rp[1][ix + 1], rp[1][ix + 2], f);
    }
    if (! ok) ++failedReads;
    return ok;
}

int StreamSource::readSequence(juce::int64 regionStart, juce::int64 regionLen, bool reverse, bool loop,
                               juce::uint64 seq, int n, float* L, float* R) {
    if (regionLen < 1) return 0;
    int produced = 0;
    while (produced < n) {
        const juce::uint64 s = seq + (juce::uint64) produced;
        if (! loop && s >= (juce::uint64) regionLen) break;
        const juce::int64 q = loop ? (juce::int64) (s % (juce::uint64) regionLen) : (juce::int64) s;
        const int run = (int) std::min<juce::int64>(n - produced, regionLen - q);
        if (! reverse) {
            read(regionStart + q, run, L + produced, R + produced);
        } else {
            read(regionStart + regionLen - q - run, run, L + produced, R + produced);
            std::reverse(L + produced, L + produced + run);
            std::reverse(R + produced, R + produced + run);
        }
        produced += run;
    }
    return produced;
}

} // namespace crowdmike::rec
