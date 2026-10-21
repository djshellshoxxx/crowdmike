#include "InputAnalysis.h"
#include "DSP/Biquad.h"
#include "DSP/Compressor.h"
#include "DSP/SlopeFilter.h"
#include "UI/EditKit.h"
#include "UI/Theme.h"
#include <cmath>

namespace crowdmike::ui {
using namespace crowdmike::params;

std::vector<float> logFrequencies(int count, float lo, float hi) {
    std::vector<float> f(static_cast<size_t>(juce::jmax(2, count)));
    for (size_t k = 0; k < f.size(); ++k)
        f[k] = lo * std::pow(hi / lo, static_cast<float>(k) / static_cast<float>(f.size() - 1));
    return f;
}

std::vector<float> computeInputResponseDb(CrowdMikeAudioProcessor& proc, int i, double sr, const std::vector<float>& freqs) {
    if (sr <= 0.0) sr = 48000.0;
    const auto v = [&](int e) { return kit::actual(proc, ParamBank::index(Mod::Input, i, e)); };
    std::vector<dsp::BiquadCoeffs> sections;
    const auto addSlope = [&](bool high, float hz, int slope) {
        const int poles = 2 * (juce::jlimit(0, 3, slope) + 1);
        for (int s = 0; s < poles / 2; ++s) {
            const auto q = static_cast<float>(dsp::SlopeFilter::butterworthQ(poles, s));
            sections.push_back(high ? dsp::designHighpass(sr, hz, q) : dsp::designLowpass(sr, hz, q));
        }
    };
    if (v(InputP::HpOn) >= 0.5f) addSlope(true, v(InputP::HpHz), juce::roundToInt(v(InputP::HpSlope)));
    if (v(InputP::LpOn) >= 0.5f) addSlope(false, v(InputP::LpHz), juce::roundToInt(v(InputP::LpSlope)));
    if (v(InputP::NotchOn) >= 0.5f) sections.push_back(dsp::designNotch(sr, v(InputP::NotchHz), v(InputP::NotchQ), v(InputP::NotchDepth)));
    for (int b = 0; b < 4; ++b) {
        const int base = InputP::Eq1On + b * kEqBandStride;
        if (v(base + EqOn) < 0.5f) continue;
        const float hz = v(base + EqHz), gain = v(base + EqGain), q = v(base + EqQ);
        switch (juce::roundToInt(v(base + EqType))) {
            case EqNotch: sections.push_back(dsp::designNotch(sr, hz, q, -std::abs(gain))); break;
            case EqLowShelf: sections.push_back(dsp::designLowShelf(sr, hz, q, gain)); break;
            case EqHighShelf: sections.push_back(dsp::designHighShelf(sr, hz, q, gain)); break;
            default: sections.push_back(dsp::designBell(sr, hz, q, gain)); break;
        }
    }
    std::vector<float> out(freqs.size(), 0.0f);
    for (size_t k = 0; k < freqs.size(); ++k) {
        double sum = 0.0;
        if (freqs[k] < sr * 0.5)
            for (const auto& c : sections) sum += dsp::biquadMagnitudeDb(c, sr, freqs[k]);
        out[k] = static_cast<float>(juce::jmax(-200.0, sum));
    }
    return out;
}

float compressorOutputDb(CrowdMikeAudioProcessor& proc, int onIdx, float inDb) {
    const auto v = [&](int e) { return kit::actual(proc, onIdx + e); };
    if (v(CompP::On) < 0.5f) return inDb;
    const float thr = v(CompP::Thr), ratio = v(CompP::Ratio), knee = v(CompP::Knee);
    const float makeup = v(CompP::AutoMakeup) >= 0.5f ? -thr * (1.0f - 1.0f / ratio) * 0.5f : v(CompP::Makeup);
    const float compressed = inDb + dsp::Compressor::computeGainDb(inDb, thr, ratio, knee) + makeup;
    const float mix = juce::jlimit(0.0f, 1.0f, v(CompP::Mix));
    // parallel mix in the linear domain
    const float wet = juce::Decibels::decibelsToGain(compressed), dry = juce::Decibels::decibelsToGain(inDb);
    return juce::Decibels::gainToDecibels(dry + mix * (wet - dry), -100.0f);
}

float sequenceCorrelation(const float* a, const float* b, int n) {
    if (n < 2) return 0.0f;
    double ma = 0.0, mb = 0.0;
    for (int k = 0; k < n; ++k) { ma += a[k]; mb += b[k]; }
    ma /= n; mb /= n;
    double sab = 0.0, saa = 0.0, sbb = 0.0;
    for (int k = 0; k < n; ++k) {
        const double da = a[k] - ma, db = b[k] - mb;
        sab += da * db; saa += da * da; sbb += db * db;
    }
    if (saa < 1.0e-12 || sbb < 1.0e-12) return 0.0f;
    return static_cast<float>(juce::jlimit(-1.0, 1.0, sab / std::sqrt(saa * sbb)));
}

namespace {
float xForHz(juce::Rectangle<float> r, float hz) { return r.getX() + r.getWidth() * std::log(hz / 20.0f) / std::log(1000.0f); }
void drawFreqGrid(juce::Graphics& g, juce::Rectangle<float> r, float dbLo, float dbHi, float dbStep) {
    const auto& p = palette();
    g.setColour(p.background.darker(0.2f));
    g.fillRoundedRectangle(r, 3.0f);
    g.setFont(font(9.5f));
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f }) {
        const float x = xForHz(r, hz);
        g.setColour(p.panelEdge.withAlpha(0.6f));
        g.drawVerticalLine(juce::roundToInt(x), r.getY(), r.getBottom());
        g.setColour(p.textMuted);
        g.drawText(kit::fmtHz(hz), juce::Rectangle<float>(x + 2.0f, r.getBottom() - 12.0f, 34.0f, 11.0f), juce::Justification::centredLeft);
    }
    const float stepPx = r.getHeight() * dbStep / (dbHi - dbLo);
    const int labelEvery = stepPx < 12.0f ? (stepPx < 6.0f ? 4 : 2) : 1;
    int k = 0;
    for (float d = dbLo; d <= dbHi + 0.01f; d += dbStep, ++k) {
        const float y = juce::jmap(d, dbLo, dbHi, r.getBottom(), r.getY());
        g.setColour(p.panelEdge.withAlpha(std::abs(d) < 0.01f ? 1.0f : 0.5f));
        g.drawHorizontalLine(juce::roundToInt(y), r.getX(), r.getRight());
        if (k % labelEvery != 0) continue;
        g.setColour(p.textMuted);
        g.drawText(juce::String(juce::roundToInt(d)), juce::Rectangle<float>(r.getX() + 2.0f, y - 11.0f, 30.0f, 10.0f), juce::Justification::centredLeft);
    }
}
void drawTitle(juce::Graphics& g, juce::Rectangle<int>& area, const juce::String& t, const juce::String& right = {}) {
    auto h = area.removeFromTop(px(16));
    g.setFont(font(11.5f, true));
    g.setColour(palette().accent);
    g.drawText(t.toUpperCase(), h, juce::Justification::centredLeft, true);
    if (right.isNotEmpty()) { g.setColour(palette().text); g.setFont(font(11.0f)); g.drawText(right, h, juce::Justification::centredRight, true); }
}
}

// ---------------------------------------------------------------- FilterResponseView
void FilterResponseView::refresh(bool force) {
    double hash = 0.0;
    for (int e = InputP::HpOn; e <= InputP::Eq4Q; ++e) hash = hash * 1.0001 + kit::actual(proc, ParamBank::index(Mod::Input, input, e)) * (e + 1);
    hash += input * 1.0e6;
    if (!force && hash == lastHash) return;
    lastHash = hash;
    if (freqs.empty()) freqs = logFrequencies(240);
    const double sr = proc.getEngine().getSampleRate();
    db = computeInputResponseDb(proc, input, sr, freqs);
    repaint();
}
void FilterResponseView::paint(juce::Graphics& g) {
    const auto& p = palette();
    auto area = getLocalBounds().reduced(px(4));
    drawTitle(g, area, "Filter / EQ response", "HPF + LPF + notch + EQ");
    auto r = area.toFloat();
    constexpr float lo = -30.0f, hi = 12.0f;
    drawFreqGrid(g, r, lo, hi, 6.0f);
    if (db.size() != freqs.size() || db.empty()) return;
    juce::Path path;
    for (size_t k = 0; k < freqs.size(); ++k) {
        const float x = xForHz(r, freqs[k]);
        const float y = juce::jmap(juce::jlimit(lo, hi, db[k]), lo, hi, r.getBottom(), r.getY());
        if (k == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    g.setColour(p.accent);
    g.strokePath(path, juce::PathStrokeType(2.0f));
    // markers with text so they do not depend on colour
    const auto mark = [&](int onE, int hzE, const char* tag) {
        if (kit::actual(proc, ParamBank::index(Mod::Input, input, onE)) < 0.5f) return;
        const float hz = kit::actual(proc, ParamBank::index(Mod::Input, input, hzE));
        const float x = xForHz(r, hz);
        g.setColour(p.text.withAlpha(0.8f));
        g.drawVerticalLine(juce::roundToInt(x), r.getY() + 12.0f, r.getBottom() - 12.0f);
        g.setFont(font(9.5f, true));
        g.drawText(tag, juce::Rectangle<float>(x - 20.0f, r.getY() + 1.0f, 40.0f, 11.0f), juce::Justification::centred);
    };
    mark(InputP::HpOn, InputP::HpHz, "HPF");
    mark(InputP::LpOn, InputP::LpHz, "LPF");
    mark(InputP::NotchOn, InputP::NotchHz, "NOTCH");
}

// ---------------------------------------------------------------- SpectrumView
void SpectrumView::paint(juce::Graphics& g) {
    const auto& p = palette();
    auto area = getLocalBounds().reduced(px(4));
    drawTitle(g, area, "Live spectrum", caption);
    auto r = area.toFloat();
    constexpr float lo = -100.0f, hi = 0.0f;
    drawFreqGrid(g, r, lo, hi, 20.0f);
    juce::Path path;
    const auto f = logFrequencies(kBins);
    for (int k = 0; k < kBins; ++k) {
        const float x = xForHz(r, f[static_cast<size_t>(k)]);
        const float y = juce::jmap(juce::jlimit(lo, hi, spectrum[static_cast<size_t>(k)]), lo, hi, r.getBottom(), r.getY());
        if (k == 0) path.startNewSubPath(x, r.getBottom());
        path.lineTo(x, y);
    }
    path.lineTo(r.getRight(), r.getBottom());
    path.closeSubPath();
    g.setColour(p.accentDim.withAlpha(0.6f));
    g.fillPath(path);
    g.setColour(p.accent);
    g.strokePath(path, juce::PathStrokeType(1.2f));
    for (float hz : markersHz) {
        const float x = xForHz(r, hz);
        g.setColour(p.danger);
        g.drawVerticalLine(juce::roundToInt(x), r.getY(), r.getBottom());
        g.setFont(font(9.5f, true));
        g.drawText(juce::String::fromUTF8("\xe2\x96\xb2") + kit::fmtHz(hz), juce::Rectangle<float>(x + 2.0f, r.getY() + 2.0f, 50.0f, 11.0f), juce::Justification::centredLeft);
    }
}

// ---------------------------------------------------------------- TransferCurveView
void TransferCurveView::paint(juce::Graphics& g) {
    const auto& p = palette();
    auto area = getLocalBounds().reduced(px(4));
    const bool on = onIndex >= 0 && kit::isOn(proc, onIndex);
    drawTitle(g, area, "Dynamics transfer", on ? "GR " + juce::String(grDb, 1) + " dB" : juce::String("COMP OFF"));
    auto r = area.toFloat();
    const float side = juce::jmin(r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre(side, side);
    g.setColour(p.background.darker(0.2f));
    g.fillRoundedRectangle(r, 3.0f);
    constexpr float lo = -60.0f, hi = 6.0f;
    const auto toX = [&](float d) { return juce::jmap(d, lo, hi, r.getX(), r.getRight()); };
    const auto toY = [&](float d) { return juce::jmap(juce::jlimit(lo, hi, d), lo, hi, r.getBottom(), r.getY()); };
    g.setFont(font(9.0f));
    for (float d = -48.0f; d <= 0.0f; d += 12.0f) {
        g.setColour(p.panelEdge.withAlpha(0.5f));
        g.drawVerticalLine(juce::roundToInt(toX(d)), r.getY(), r.getBottom());
        g.drawHorizontalLine(juce::roundToInt(toY(d)), r.getX(), r.getRight());
        g.setColour(p.textMuted);
        g.drawText(juce::String(juce::roundToInt(d)), juce::Rectangle<float>(toX(d) + 1.0f, r.getBottom() - 11.0f, 24.0f, 10.0f), juce::Justification::centredLeft);
    }
    g.setColour(p.panelEdge);
    g.drawLine(toX(lo), toY(lo), toX(hi), toY(hi), 1.0f);     // unity reference
    if (onIndex < 0) return;
    juce::Path path;
    for (int k = 0; k <= 96; ++k) {
        const float in = lo + (hi - lo) * static_cast<float>(k) / 96.0f;
        const float out = compressorOutputDb(proc, onIndex, in);
        if (k == 0) path.startNewSubPath(toX(in), toY(out)); else path.lineTo(toX(in), toY(out));
    }
    g.setColour(on ? p.accent : p.textMuted);
    g.strokePath(path, juce::PathStrokeType(2.0f));
    if (on) {
        const float thr = kit::actual(proc, onIndex + CompP::Thr);
        g.setColour(p.warn);
        g.fillEllipse(toX(thr) - 3.0f, toY(compressorOutputDb(proc, onIndex, thr)) - 3.0f, 6.0f, 6.0f);
        g.setFont(font(9.5f, true));
        g.drawText("THR " + juce::String(thr, 1), juce::Rectangle<float>(toX(thr) + 4.0f, toY(thr) + 2.0f, 70.0f, 11.0f), juce::Justification::centredLeft);
    }
}

// ---------------------------------------------------------------- PairCorrelationView
void PairCorrelationView::paint(juce::Graphics& g) {
    const auto& p = palette();
    auto area = getLocalBounds().reduced(px(4));
    drawTitle(g, area, "Correlation / polarity", pair ? juce::String(value, 2) : juce::String("mono"));
    g.setFont(font(11.0f));
    g.setColour(p.textMuted);
    auto textArea = area.removeFromBottom(juce::jmin(area.getHeight() / 2, px(34)));
    g.drawFittedText(label, textArea, juce::Justification::centredLeft, 3);
    if (!pair) return;
    auto bar = area.reduced(px(4), juce::jmax(0, area.getHeight() / 2 - px(10))).toFloat();
    g.setColour(p.panelRaised);
    g.fillRoundedRectangle(bar, 3.0f);
    const float cx = bar.getCentreX();
    const float x = juce::jmap(juce::jlimit(-1.0f, 1.0f, value), -1.0f, 1.0f, bar.getX(), bar.getRight());
    g.setColour(value < 0.0f ? p.danger : p.ok);
    g.fillRect(juce::Rectangle<float>(juce::jmin(cx, x), bar.getY() + 3.0f, std::abs(x - cx), bar.getHeight() - 6.0f));
    g.setColour(p.text);
    g.drawVerticalLine(juce::roundToInt(cx), bar.getY(), bar.getBottom());
    g.setFont(font(9.5f, true));
    g.drawText("-1", bar.reduced(3.0f, 0.0f), juce::Justification::centredLeft);
    g.drawText("+1", bar.reduced(3.0f, 0.0f), juce::Justification::centredRight);
}
}
