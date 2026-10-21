#pragma once
// LoudnessMeter: K-weighted short-term loudness approximation + 400 ms RMS of a stereo bus (SHOW_REPORT_SPEC section 4).
//
// * Stage 1 (high shelf) and stage 2 (RLB high-pass) of ITU-R BS.1770 for any sample rate (bilinear design).
// * Per-sample K-weighted power of both channels is averaged (channel weights 1.0) and accumulated in 0.1 s sub-blocks; the
//   short-term value is the mean of the newest 30 sub-blocks (3 s). No gating, so it approximates EBU R128 short-term
//   loudness only for steady material. LUFS = -0.691 + 10 log10(meanSquare).
// * The momentary RMS is the unweighted mean power of the newest 4 sub-blocks (400 ms) in dBFS.
// * prepare() may allocate nothing but is not real-time; push() is noexcept, allocation-free and lock-free, treats non-finite
//   samples as 0 and works for any block size >= 0 and sample rates 44.1-192 kHz. reset() is deterministic.
// * Getters are relaxed-atomic reads (any thread). Values are floored at -120.
#include <atomic>
#include <array>

namespace crowdmike::dsp {
class LoudnessMeter {
public:
    static constexpr float kFloorDb = -120.0f;
    static constexpr int kShortBlocks = 30;      // 3 s of 0.1 s blocks
    static constexpr int kMomentaryBlocks = 4;   // 400 ms

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void push(const float* left, const float* right, int n) noexcept;

    float getShortTermLufs() const noexcept { return shortLufs.load(std::memory_order_relaxed); }
    float getMomentaryRmsDb() const noexcept { return momentaryDb.load(std::memory_order_relaxed); }

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double process(double x) noexcept {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void clear() noexcept { z1 = z2 = 0.0; }
    };
    void design() noexcept;
    void closeBlock() noexcept;

    double sr = 48000.0;
    int blockLen = 4800;
    Biquad shelf[2], rlb[2];
    int fill = 0;
    double sumK = 0.0, sumRaw = 0.0;
    std::array<double, kShortBlocks> ringK {}, ringRaw {};
    int ringPos = 0, ringCount = 0;
    std::atomic<float> shortLufs { kFloorDb }, momentaryDb { kFloorDb };
};
}
