#include "ChannelDelay.h"
#include "DspUtil.h"

namespace crowdmike::dsp {
void ChannelDelay::prepare(double sampleRate, int maxBlock, int channels, float maxDelayMs) {
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    numChannels = std::max(0, channels);
    maxBlockSize = std::max(1, maxBlock);
    const float ms = util::isFiniteBits(maxDelayMs) ? std::max(0.0f, maxDelayMs) : 0.0f;
    maxDelay = static_cast<int>(std::ceil(ms * 0.001 * sr));
    size = 1;
    while (size < maxDelay + 4) size <<= 1;
    mask = size - 1;
    buf.assign(static_cast<size_t>(numChannels) * static_cast<size_t>(size), 0.0f);
    traj.assign(static_cast<size_t>(maxBlockSize), 0.0);
    reset();
}

void ChannelDelay::reset() noexcept {
    std::fill(buf.begin(), buf.end(), 0.0f);
    writePos = 0;
    target = remaining = 0;
    curD = step = 0.0;
    first = true;
}

void ChannelDelay::process(float* const* ch, int nch, int n, float delayMs) noexcept {
    nch = std::min(nch, numChannels);
    if (ch == nullptr || nch <= 0 || n <= 0) return;
    const double ms = util::isFiniteBits(delayMs) ? std::max(0.0f, delayMs) : 0.0;
    const int d = std::min(maxDelay, static_cast<int>(std::lround(ms * 0.001 * sr)));
    if (first) {
        first = false;
        target = d;
        curD = d;
        remaining = 0;
    } else if (d != target) {
        target = d;
        const double rampMs = std::max(30.0, 4.0 * std::fabs(d - curD) * 1000.0 / sr);
        remaining = std::max(1, static_cast<int>(rampMs * 0.001 * sr));
        step = (d - curD) / remaining;
    }
    if (remaining == 0 && target == 0) {
        // Idle: the delay memory is frozen, only the last 4 input samples are kept just behind the write position so a
        // later glide out of 0 ms (which never reads further back than that) continues seamlessly.
        const int first4 = std::max(0, n - 4);
        for (int c = 0; c < nch; ++c) {
            float* b = buf.data() + static_cast<size_t>(c) * static_cast<size_t>(size);
            float* x = ch[c];
            for (int i = first4; i < n; ++i) {
                x[i] = util::cleanSample(x[i]);
                b[(writePos - 4) & mask] = b[(writePos - 3) & mask];
                b[(writePos - 3) & mask] = b[(writePos - 2) & mask];
                b[(writePos - 2) & mask] = b[(writePos - 1) & mask];
                b[(writePos - 1) & mask] = x[i];
            }
            util::cleanBuffer(x, first4);
        }
        return;
    }
    for (int off = 0; off < n; off += maxBlockSize) processChunk(ch, nch, off, std::min(maxBlockSize, n - off));
}

void ChannelDelay::processChunk(float* const* ch, int nch, int offset, int n) noexcept {
    const int wp0 = writePos;
    if (remaining == 0) {
        const int dd = target;
        for (int c = 0; c < nch; ++c) {
            float* b = buf.data() + static_cast<size_t>(c) * static_cast<size_t>(size);
            float* x = ch[c] + offset;
            int w = wp0;
            for (int i = 0; i < n; ++i) {
                b[w] = util::cleanSample(x[i]);
                x[i] = b[(w - dd) & mask];
                w = (w + 1) & mask;
            }
        }
    } else {
        for (int i = 0; i < n; ++i) {
            if (remaining > 0) {
                curD += step;
                if (--remaining == 0) curD = target;
            }
            traj[static_cast<size_t>(i)] = curD;
        }
        for (int c = 0; c < nch; ++c) {
            float* b = buf.data() + static_cast<size_t>(c) * static_cast<size_t>(size);
            float* x = ch[c] + offset;
            int w = wp0;
            for (int i = 0; i < n; ++i) {
                b[w] = util::cleanSample(x[i]);
                const double dl = traj[static_cast<size_t>(i)];
                const int i0 = static_cast<int>(dl);
                const float frac = static_cast<float>(dl - i0);
                const float s0 = b[(w - i0) & mask];
                const float s1 = b[(w - i0 - 1) & mask];
                x[i] = s0 + frac * (s1 - s0);
                w = (w + 1) & mask;
            }
        }
    }
    writePos = (wp0 + n) & mask;
}
}
