#include "TempoWidget.h"
#include "Core/ParamBank.h"

namespace crowdmike::ui {
using namespace crowdmike;
using namespace crowdmike::params;

TempoWidget::TempoWidget(EditorContext& c)
    : proc(c.proc),
      source(c.proc, ParamBank::index(Mod::Tempo, 0, TempoP::Source)),
      quantise(c.proc, ParamBank::index(Mod::Tempo, 0, TempoP::Quantise)),
      tapBtn(c.proc, ParamBank::index(Mod::Tempo, 0, TempoP::Tap), "TAP") {
    source.setComponentID("tempo.source");
    source.setTooltip("Tempo source: Host playhead, Tap, Manual, MIDI clock or Auto-detect from audio");
    quantise.setComponentID("tempo.quantise");
    quantise.setTooltip("Quantise Echo Throw, Reverse and clip triggers to the beat grid");
    bpmLabel.setComponentID("tempo.bpm");
    bpmLabel.setFont(monoFont(15.0f));
    bpmLabel.setColour(juce::Label::textColourId, palette().text);
    bpmLabel.setJustificationType(juce::Justification::centred);
    bpmLabel.setMinimumHorizontalScale(0.6f);
    bpmLabel.setInterceptsMouseClicks(false, false);
    tapBtn.setComponentID("tempo.tap");
    tapBtn.setTooltip("Tap the tempo (also MIDI / OSC /crowdmike/tempo/tap)");
    addAndMakeVisible(source);
    addAndMakeVisible(quantise);
    addAndMakeVisible(bpmLabel);
    addAndMakeVisible(tapBtn);
    setTitle("Tempo");
    tick();
}

void TempoWidget::resized() {
    auto r = getLocalBounds();
    source.setBounds(r.removeFromLeft(110).reduced(0, 1)); r.removeFromLeft(4);
    bpmLabel.setBounds(r.removeFromLeft(78)); r.removeFromLeft(2);
    tapBtn.setBounds(r.removeFromLeft(50).reduced(0, 1)); r.removeFromLeft(8);
    ledArea = r.removeFromLeft(16).withSizeKeepingCentre(14, 14); r.removeFromLeft(6);
    confArea = r.removeFromLeft(40).withSizeKeepingCentre(40, 8); r.removeFromLeft(8);
    quantise.setBounds(r.reduced(0, 1));
}

void TempoWidget::paint(juce::Graphics& g) {
    const auto& p = palette();
    g.setColour(ledOn ? (ledDownbeat ? p.accent : p.ok) : p.panelEdge);
    if (ledOn) g.fillEllipse(ledArea.toFloat());
    else g.drawEllipse(ledArea.toFloat().reduced(1.0f), 1.5f);
    g.setColour(p.panelEdge);
    g.drawRoundedRectangle(confArea.toFloat(), 2.0f, 1.0f);
    g.setColour(lowConf ? p.warn : p.ok);
    g.fillRoundedRectangle(confArea.toFloat().reduced(1.0f).withWidth(juce::jmax(0.0f, (static_cast<float>(confArea.getWidth()) - 2.0f) * confShown)), 1.5f);
}

void TempoWidget::tick() {
    const auto s = proc.getTempo().snapshot();
    juce::String text = s.hasTempo ? juce::String(s.bpm, 1) : juce::String("--.-");
    if (s.hasTempo && s.lowConfidence) text << " ?";
    if (bpmLabel.getText() != text) bpmLabel.setText(text, juce::dontSendNotification);
    const double beat = s.ppq - std::floor(s.ppq);
    const bool on = s.hasTempo && s.playing && beat < 0.2;
    const int beatIndex = static_cast<int>(std::floor(s.ppq));
    const int bar = juce::jmax(1, s.barBeats);
    const bool down = on && (((beatIndex % bar) + bar) % bar) == 0;
    const float conf = juce::jlimit(0.0f, 1.0f, s.confidence);
    if (on != ledOn || down != ledDownbeat || s.lowConfidence != lowConf || std::abs(conf - confShown) > 0.01f) {
        ledOn = on; ledDownbeat = down; lowConf = s.lowConfidence; confShown = conf;
        repaint();
    }
}
}
