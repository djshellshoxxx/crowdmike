#pragma once
// EqBank: N-band serial parametric EQ driven by the CM_EQBAND layout (stride kEqBandStride = 5: On, Type, Hz, Gain, Q).
// Invariants:
//  * bandParams points at band 1's `On` entry; band b's field f is bandParams[b * kEqBandStride + f]. bandParams may be
//    nullptr (all bands treated as off). numBands is clamped to the maxBands given to prepare().
//  * Types: Bell (gain at Hz == Gain dB), Notch (finite-depth notch, depth = -|Gain| dB at Hz, bandwidth Hz/Q),
//    LowShelf / HighShelf (RBJ, Gain at DC / Nyquist).
//  * Smoothing: Hz and Q glide in the log domain (50 ms), Gain over 30 ms; coefficients are recomputed every
//    kControlInterval samples and only while a value moves. Control ticks are counted across blocks (block-size
//    independent output).
//  * Per-band On/Off crossfades over 20 ms. A Type change fades the band out, switches type, then fades back in.
//    Fade completion is handled on control ticks, so all state transitions are block-size independent.
//  * A band whose fade has settled at 0 costs nothing. With all bands settled off the buffer is untouched (bit-exact,
//    except non-finite samples which become 0). Channels >= prepared count are untouched.
#include "Biquad.h"
#include "DspCommon.h"
#include <vector>

namespace crowdmike::dsp {
class EqBank {
public:
    static constexpr int kControlInterval = 16;
    void prepare(double sampleRate, int maxBlock, int channels, int maxBands);
    void reset() noexcept;
    void process(float* const* ch, int nch, int n, const float* bandParams, int numBands) noexcept;

private:
    struct Band {
        int type = 0, reqType = 0;
        bool reqOn = false;
        float reqHz = 1000.0f, reqGain = 0.0f, reqQ = 1.0f;
        bool active = false;       // filter running (wet > 0 or fading)
        Smoother wet, logHz, gainDb, logQ;
        bool dirty = true;
        BiquadCoeffs coeffs;
    };
    void designBand(Band& b) noexcept;
    void settleBand(int index) noexcept;   // band faded out: apply pending type, snap params, (de)activate

    double sr = 48000.0;
    int numChannels = 0, maxBands = 0, countdown = 0;
    bool first = true, bypassed = false;
    std::vector<Band> bands;
    std::vector<Biquad> filters;   // [band][channel]
};
}
