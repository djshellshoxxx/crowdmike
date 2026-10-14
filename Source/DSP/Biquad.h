#pragma once
// Biquad: RBJ-cookbook designers + transposed direct-form-II biquad.
// Invariants:
//  * Coefficients are normalised (a0 == 1) and stored in DOUBLE precision, state is double. This is a deliberate
//    deviation from the float `BiquadCoeffs` sketched in docs/MODULES.md: float coefficients cannot represent a
//    20 Hz high-pass at 192 kHz (a1 = -2cos(w0) differs from -2 by less than one float ULP), double can.
//  * Designers clamp frequency to [1 Hz, 0.49 * sr] and Q to [0.01, 1000]; they never return a non-finite or unstable
//    filter for finite inputs (non-finite inputs fall back to safe defaults).
//  * designNotch(): finite-depth notch. Denominator bandwidth = hz/Q; gain at hz == depthDb exactly (depthDb <= 0).
//  * designBell(): gain at hz == gainDb exactly. Shelves: gainDb at DC (low) / Nyquist (high), gainDb/2 at hz.
//  * Biquad::process() is noexcept and allocation free. Callers sanitise input (see DspUtil.h).
//  * BiquadCascade: up to 4 sections sharing coefficients over N channels; prepare() allocates, the rest does not.
#include <vector>

namespace crowdmike::dsp {
struct BiquadCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;   // normalised, a0 == 1
};

BiquadCoeffs designLowpass(double sr, float hz, float q) noexcept;
BiquadCoeffs designHighpass(double sr, float hz, float q) noexcept;
BiquadCoeffs designBandpass(double sr, float hz, float q) noexcept;          // constant 0 dB peak gain
BiquadCoeffs designBell(double sr, float hz, float q, float gainDb) noexcept;
BiquadCoeffs designNotch(double sr, float hz, float q, float depthDb) noexcept;
BiquadCoeffs designLowShelf(double sr, float hz, float q, float gainDb) noexcept;
BiquadCoeffs designHighShelf(double sr, float hz, float q, float gainDb) noexcept;
BiquadCoeffs designAllpass(double sr, float hz, float q) noexcept;

// |H(e^jw)| in dB and phase in radians at `hz` (analysis/GUI/test helper, not real-time critical).
double biquadMagnitudeDb(const BiquadCoeffs& c, double sr, double hz) noexcept;
double biquadPhase(const BiquadCoeffs& c, double sr, double hz) noexcept;

class Biquad {
public:
    void setCoeffs(const BiquadCoeffs& c) noexcept { k = c; }
    const BiquadCoeffs& getCoeffs() const noexcept { return k; }
    void reset() noexcept { s1 = s2 = 0.0; }
    float process(float x) noexcept {
        const double xd = x;
        const double y = k.b0 * xd + s1;
        s1 = k.b1 * xd - k.a1 * y + s2;
        s2 = k.b2 * xd - k.a2 * y;
        return static_cast<float>(y);
    }
    void process(float* data, int n) noexcept;   // in place
    void flushDenormals() noexcept;              // zero tiny state (call once per block)
private:
    BiquadCoeffs k;
    double s1 = 0.0, s2 = 0.0;
};

// Multi-channel cascade of up to kMaxSections biquads with coefficients shared by all channels.
class BiquadCascade {
public:
    static constexpr int kMaxSections = 4;
    void prepare(int numChannels);                                    // allocates
    void reset() noexcept;
    void setNumSections(int n) noexcept;                              // clamped 0..kMaxSections
    int getNumSections() const noexcept { return sections; }
    void setSection(int i, const BiquadCoeffs& c) noexcept;
    const BiquadCoeffs& getSection(int i) const noexcept { return coeffs[i]; }
    void process(int channel, float* data, int n) noexcept;           // in place; channel >= prepared -> untouched
    int getNumChannels() const noexcept { return channels; }
private:
    BiquadCoeffs coeffs[kMaxSections];
    int sections = 0, channels = 0;
    std::vector<double> state;   // [channel][section][2]
};
}
