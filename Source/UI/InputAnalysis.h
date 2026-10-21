#pragma once
// Analysis views for the Inputs page "advanced" section (GUI_SPEC section 4): combined filter/EQ magnitude response,
// live spectrum, compressor transfer curve and a level-envelope correlation meter for linked pairs.
// Every view is passive: the owner feeds/refreshes it from its own Tickable (no per-component timers).
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <functional>
#include <vector>
#include "PluginProcessor.h"

namespace crowdmike::ui {
// Magnitude (dB) of the input's HPF + LPF + notch + 4 EQ bands at each frequency, from the current parameter values.
std::vector<float> computeInputResponseDb(CrowdMikeAudioProcessor&, int input, double sampleRate, const std::vector<float>& freqsHz);
// Static compressor curve for the CompP block whose `On` parameter has global index `compOnIndex`: output level (dB) for
// `inDb`, including makeup (auto-makeup rule of DSP/Compressor.h).
float compressorOutputDb(CrowdMikeAudioProcessor&, int compOnIndex, float inDb);
// Pearson correlation of two equally long sequences (0 when either is constant).
float sequenceCorrelation(const float* a, const float* b, int n);
// Log-spaced frequencies.
std::vector<float> logFrequencies(int count, float lo = 20.0f, float hi = 20000.0f);

class FilterResponseView final : public juce::Component {
public:
    explicit FilterResponseView(CrowdMikeAudioProcessor& p) : proc(p) { setTitle("Filter response"); }
    void setInput(int i) { input = i; refresh(true); }
    void refresh(bool force = false);       // recomputes when parameters changed
    void paint(juce::Graphics&) override;
    const std::vector<float>& curve() const noexcept { return db; }
private:
    CrowdMikeAudioProcessor& proc;
    int input = 0;
    std::vector<float> freqs, db;
    double lastHash = -1.0;
};

class SpectrumView final : public juce::Component {
public:
    SpectrumView() { setTitle("Spectrum"); spectrum.assign(kBins, -120.0f); }
    static constexpr int kBins = 160;
    std::function<void(float*, int)> source;           // fills kBins log-spaced bins 20 Hz..20 kHz (dBFS)
    std::vector<float> markersHz;                     // feedback candidates
    juce::String caption;
    void refresh() { if (source) { source(spectrum.data(), kBins); repaint(); } }
    void paint(juce::Graphics&) override;
private:
    std::vector<float> spectrum;
};

class TransferCurveView final : public juce::Component {
public:
    TransferCurveView(CrowdMikeAudioProcessor& p) : proc(p) { setTitle("Compressor transfer curve"); }
    void setCompBlock(int compOnIndex) { onIndex = compOnIndex; repaint(); }
    void setGainReduction(float db) { if (std::abs(db - grDb) > 0.05f) { grDb = db; repaint(); } }
    void refresh() { repaint(); }
    void paint(juce::Graphics&) override;
private:
    CrowdMikeAudioProcessor& proc;
    int onIndex = -1;
    float grDb = 0.0f;
};

// Correlation of two inputs' level envelopes plus their polarity flags (an estimate; see tooltip).
class PairCorrelationView final : public juce::Component, public juce::SettableTooltipClient {
public:
    PairCorrelationView() { setTitle("Pair correlation"); }
    void set(bool isPair, float correlation, const juce::String& text) { pair = isPair; value = correlation; label = text; repaint(); }
    void paint(juce::Graphics&) override;
private:
    bool pair = false;
    float value = 0.0f;
    juce::String label;
};
}
