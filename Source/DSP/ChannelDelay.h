#pragma once
// ChannelDelay: multi-channel alignment/reinforcement delay line.
// Signature: prepare(sr, maxBlock, channels, maxDelayMs); process(ch, nch, n, delayMs) (docs/MODULES.md).
// Invariants:
//  * Target delay D = round(sr * delayMs / 1000) samples (delayMs clamped to [0, maxDelayMs]). When static the output
//    is the input delayed by exactly D samples (bit-exact integer delay, no interpolation).
//  * When delayMs changes the read position glides linearly (varispeed, linear interpolation only while gliding) to
//    the new integer delay over max(30 ms, 4 * |change in ms|), i.e. at most 25 % speed change: continuous output,
//    no clicks. The glide ends exactly on the integer target.
//  * At 0 ms (settled) the audio is untouched (bit-exact; non-finite -> 0) and only the last 4 input samples per channel
//    are stored; leaving 0 ms is still seamless because a glide from 0 (< 25 % speed change) never reads further back.
//  * Channels beyond prepare()'s count are untouched. reset() clears memory and snaps to the next requested delay.
#include "DspCommon.h"
#include <vector>

namespace crowdmike::dsp {
class ChannelDelay {
public:
    void prepare(double sampleRate, int maxBlock, int channels, float maxDelayMs);
    void reset() noexcept;
    void process(float* const* ch, int nch, int n, float delayMs) noexcept;
    int getDelaySamples() const noexcept { return target; }   // integer target delay
    bool isGliding() const noexcept { return remaining > 0; }

private:
    void processChunk(float* const* ch, int nch, int offset, int n) noexcept;

    double sr = 48000.0;
    int numChannels = 0, size = 1, mask = 0, maxDelay = 0, maxBlockSize = 0, writePos = 0;
    int target = 0, remaining = 0;
    double curD = 0.0, step = 0.0;
    bool first = true;
    std::vector<float> buf;    // [channel][size]
    std::vector<double> traj;  // per-sample delay of the current chunk (maxBlock)
};
}
