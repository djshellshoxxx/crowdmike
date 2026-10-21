#include "LiveStrips.h"
#include "DSP/RouteRisk.h"
#include "Engine/TapMap.h"

namespace crowdmike::ui {
using namespace crowdmike::params;

namespace {
const juce::String kArrow = juce::String::fromUTF8("\xe2\x86\x92");
int scaled(float v) { return juce::roundToInt(v * scale()); }
void select(EditorContext& ctx, Selection::Kind k, int i) { ctx.select({ k, i }); }
}

// ================================================================= helpers
void styleLiveLabel(juce::Label& l, float h, bool bold, juce::Colour c, juce::Justification j) {
    l.setFont(font(h, bold));
    l.setColour(juce::Label::textColourId, c);
    l.setJustificationType(j);
    l.setMinimumHorizontalScale(0.55f);
    l.setBorderSize({ 0, 1, 0, 1 });
}

juce::String formatDb(float db, int decimals) {
    if (!std::isfinite(db) || db <= -99.0f) return "-inf";
    return (db > 0.0f ? "+" : "") + juce::String(db, decimals);
}

juce::String inputDestinationText(CrowdMikeAudioProcessor& proc, int i) {
    auto& r = proc.getRouting();
    juce::StringArray groups;
    for (int g = 0; g < kMaxGroups; ++g)
        if (r.getAssign(i, g) > 0.0f) groups.add("G" + juce::String(g + 1));
    bool direct = false;
    for (int o = 0; o < proc.getActiveOutputChannelCount(); ++o) direct = direct || r.getDirect(i, o) != 0.0f;
    if (groups.isEmpty()) return direct ? "direct" : "no route";
    return groups.joinIntoString(" ") + (direct ? " +dir" : "");
}

bool isInputRecordArmed(const ShowModel& show, int i) {
    for (const auto& t : show.recorder.tracks)
        if (t.sourceId == TapMap::forRawInput(i) && t.armed) return true;
    return false;
}

bool setInputRecordArmed(ShowModel& show, int i, bool armed) {
    const int src = TapMap::forRawInput(i);
    for (auto& t : show.recorder.tracks)
        if (t.sourceId == src) {
            if (armed) t.armed = true;
            else t = RecTrackSetting {};
            return true;
        }
    if (!armed) return true;
    for (auto& t : show.recorder.tracks)
        if (t.sourceId < 0) {
            t = RecTrackSetting {};
            t.sourceId = src;
            t.numChannels = 1;
            t.armed = true;
            t.name = show.inputDisplayName(i);
            return true;
        }
    return false;
}

void riskBadgeFor(float score, juce::String& text, StatusBadge::Level& level) {
    const int pct = juce::roundToInt(juce::jlimit(0.0f, 1.0f, score) * 100.0f);
    if (score >= dsp::risk::kHighThreshold) { text = "FB HIGH " + juce::String(pct); level = StatusBadge::Level::Danger; }
    else if (score >= dsp::risk::kCautionThreshold) { text = "FB CAUT " + juce::String(pct); level = StatusBadge::Level::Warn; }
    else { text = "FB low " + juce::String(pct); level = StatusBadge::Level::Off; }
}

// ================================================================= FlowRibbon
FlowRibbon::FlowRibbon(EditorContext& c) : ctx(c) {
    static const char* names[kStages] = { "INPUT", "ALIGN", "CLEAN", "SHAPE", "FX", "MASTER", "OUTPUT" };
    static const char* tips[kStages] = {
        "Input stage: trim, polarity, source - opens the Inputs page",
        "Alignment delay - opens the Inputs page",
        "Clean-up (filters, notch, gate, de-esser) - opens the Inputs page",
        "Shape: crowd groups (dynamics, width, sends) - opens the Groups page",
        "Effects rack - opens the FX page",
        "Master crowd bus - shows the master in the inspector",
        "Output matrix and protection - opens the Matrix page" };
    for (int i = 0; i < kStages; ++i) {
        auto* b = buttons.add(new juce::TextButton(names[i]));
        b->setTooltip(tips[i]);
        b->setTitle(juce::String("Signal flow: ") + names[i]);
        b->setWantsKeyboardFocus(true);
        b->onClick = [this, i] {
            switch (i) {
                case 0: case 1: case 2: if (ctx.navigate) ctx.navigate(PageId::Inputs); break;
                case 3: if (ctx.navigate) ctx.navigate(PageId::Groups); break;
                case 4: if (ctx.navigate) ctx.navigate(PageId::Fx); break;
                case 5: ctx.select({ Selection::Kind::Master, 0 }); ctx.say("Master bus selected in the inspector"); break;
                default: if (ctx.navigate) ctx.navigate(PageId::Matrix); break;
            }
        };
        addAndMakeVisible(b);
    }
}

void FlowRibbon::resized() {
    auto r = getLocalBounds().reduced(4, 3);
    const int gap = juce::jmax(14, r.getWidth() / 50);
    const int w = (r.getWidth() - gap * (kStages - 1)) / kStages;
    for (int i = 0; i < kStages; ++i) {
        buttons[i]->setBounds(r.removeFromLeft(w));
        r.removeFromLeft(gap);
    }
}

void FlowRibbon::paint(juce::Graphics& g) {
    g.fillAll(palette().panel);
    g.setColour(palette().accent);
    for (int i = 0; i + 1 < kStages; ++i) {
        const auto a = buttons[i]->getBounds(), b = buttons[i + 1]->getBounds();
        const float x0 = static_cast<float>(a.getRight()) + 2.0f, x1 = static_cast<float>(b.getX()) - 2.0f;
        const float y = static_cast<float>(a.getCentreY());
        g.drawArrow({ x0, y, x1, y }, 2.0f, 8.0f, juce::jmin(8.0f, (x1 - x0) * 0.6f));
    }
}

// ================================================================= LiveInputStrip
LiveInputStrip::LiveInputStrip(EditorContext& c, int i, const LiveFlag& f)
    : ctx(c), proc(c.proc), index(i), live(f),
      trim(c.proc, ParamBank::index(Mod::Input, i, InputP::Trim), juce::Slider::LinearVertical, 60),
      mute(c.proc, ParamBank::index(Mod::Input, i, InputP::Mute), "M"),
      solo(c.proc, ParamBank::index(Mod::Input, i, InputP::Solo), "S"),
      pol(c.proc, ParamBank::index(Mod::Input, i, InputP::Pol), juce::String::fromUTF8("\xc3\x98")) {
    const juce::String n = "Input " + juce::String(i + 1);
    styleLiveLabel(name, 13.0f, true, palette().text, juce::Justification::centred);
    name.setEditable(false, true, false);
    name.setTooltip(n + ": double-click to rename");
    name.setTitle(n + " name");
    name.onTextChange = [this] {
        proc.getShow().input(index).name = name.getText().trim();
        ctx.showModelChanged();
    };
    for (auto* l : { &dest, &delay, &readout }) styleLiveLabel(*l, 11.5f, false, palette().textMuted, juce::Justification::centred);
    readout.setFont(monoFont(11.5f));
    readout.setColour(juce::Label::textColourId, palette().text);
    dest.setTooltip(n + ": crowd-group destinations (G = group, dir = direct output route)");
    delay.setTooltip(n + ": alignment delay");
    readout.setTooltip(n + ": peak level (dBFS) and compressor gain reduction (dB)");
    history.setTitle(n + " level history");
    history.snapshot = [this](float* mn, float* mx, float* rms, int b) {
        if (live.on) proc.getEngine().inputHistory(index).snapshot(mn, mx, rms, b);
    };
    meter.setTitle(n + " meter");
    meter.setTooltip(n + " peak meter. Red bar on top = clip latched; click to clear");
    meter.source = [this] {
        if (!live.on) return 0.0f;
        auto& m = proc.getMeters();
        float pk = m.inPeak[static_cast<size_t>(index)].getAndReset();
        if (m.inClip[static_cast<size_t>(index)].exchange(false, std::memory_order_relaxed)) pk = juce::jmax(pk, 1.0f);
        return pk;
    };
    gr.source = [this] { return live.on ? std::abs(proc.getMeters().inGrDb[static_cast<size_t>(index)].load(std::memory_order_relaxed)) : gr.shownDb(); };
    gr.setTitle(n + " compressor gain reduction");
    trim.setTooltip(n + " trim (-60..+24 dB). Shift-drag fine, Ctrl-click reset, double-click to type");
    mute.setTooltip(n + " mute");
    solo.setTooltip(n + " solo");
    pol.setTooltip(n + " polarity invert");
    recArm.setTooltip(n + ": arm / disarm a raw-input recorder track");
    recArm.setTitle(n + " record arm");
    recArm.setWantsKeyboardFocus(true);
    recArm.onClick = [this] {
        auto& show = proc.getShow();
        const bool want = !isInputRecordArmed(show, index);
        if (!setInputRecordArmed(show, index, want)) { ctx.say("No free recorder track for " + show.inputDisplayName(index), true); return; }
        ctx.showModelChanged();
        tick();
    };
    risk.setTitle(n + " feedback risk");
    for (juce::Component* comp : std::initializer_list<juce::Component*> { &name, &dest, &delay, &readout, &history, &meter, &gr, &trim, &mute, &solo, &pol, &risk, &recArm })
        addAndMakeVisible(comp);
    name.addMouseListener(this, false);
    history.addMouseListener(this, false);
    setTitle(n + " strip");
    setWantsKeyboardFocus(false);
    refreshNames();
    tick();
}
LiveInputStrip::~LiveInputStrip() = default;

void LiveInputStrip::refreshNames() { name.setText(proc.getShow().inputDisplayName(index), juce::dontSendNotification); }

void LiveInputStrip::mouseDown(const juce::MouseEvent&) { select(ctx, Selection::Kind::Input, index); }

void LiveInputStrip::tick() {
    dest.setText(kArrow + " " + inputDestinationText(proc, index), juce::dontSendNotification);
    const float ms = proc.getBank().rawAt(ParamBank::index(Mod::Input, index, InputP::AlignMs));
    delay.setText("Dly " + juce::String(ms, ms < 10.0f ? 2 : 1) + " ms", juce::dontSendNotification);
    readout.setText(formatDb(meter.getDisplayedLevelDb()) + " GR " + juce::String(gr.shownDb(), 1), juce::dontSendNotification);
    juce::String t; StatusBadge::Level l;
    riskBadgeFor(proc.getMeters().inRisk[static_cast<size_t>(index)].load(std::memory_order_relaxed), t, l);
    risk.set(t, l);
    const bool armed = isInputRecordArmed(proc.getShow(), index);
    const auto txt = armed ? juce::String::fromUTF8("\xe2\x97\x8f REC ARMED") : juce::String::fromUTF8("\xe2\x97\x8b rec arm");
    if (recArm.getButtonText() != txt) {
        recArm.setButtonText(txt);
        recArm.setToggleState(armed, juce::dontSendNotification);
        recArm.setColour(juce::TextButton::buttonOnColourId, palette().rec.withAlpha(0.55f));
    }
    const bool sel = ctx.getSelection() == Selection { Selection::Kind::Input, index };
    if (sel != (getProperties()["sel"].toString() == "1")) { getProperties().set("sel", sel ? "1" : "0"); repaint(); }
}

void LiveInputStrip::paint(juce::Graphics& g) {
    const auto& p = palette();
    const bool sel = ctx.getSelection() == Selection { Selection::Kind::Input, index };
    auto b = getLocalBounds().toFloat().reduced(1.5f);
    g.setColour(p.panel);
    g.fillRoundedRectangle(b, 5.0f);
    g.setColour(sel ? p.accent : p.panelEdge);
    g.drawRoundedRectangle(b, 5.0f, sel ? 2.5f : 1.0f);
    if (sel) {   // selection is also marked by a text tab, not just the outline colour
        g.setFont(font(10.0f, true));
        g.drawText("SEL", b.toNearestInt().removeFromBottom(scaled(12)).removeFromRight(scaled(26)), juce::Justification::centred);
    }
}

void LiveInputStrip::resized() {
    auto r = getLocalBounds().reduced(5);
    const int h = r.getHeight();
    const int row = juce::jlimit(16, 24, h / 22);
    name.setBounds(r.removeFromTop(row + 2));
    history.setBounds(r.removeFromTop(juce::jlimit(18, 48, h / 12)).reduced(0, 2));
    recArm.setBounds(r.removeFromBottom(row + 2).reduced(0, 1));
    risk.setBounds(r.removeFromBottom(row).reduced(0, 1));
    auto tog = r.removeFromBottom(row + 4).reduced(0, 1);
    const int tw = tog.getWidth() / 3;
    mute.setBounds(tog.removeFromLeft(tw).reduced(1, 0));
    solo.setBounds(tog.removeFromLeft(tw).reduced(1, 0));
    pol.setBounds(tog.reduced(1, 0));
    delay.setBounds(r.removeFromBottom(row - 2));
    dest.setBounds(r.removeFromBottom(row - 2));
    readout.setBounds(r.removeFromBottom(row - 2));
    r.removeFromTop(3);
    auto meters = r.removeFromLeft(juce::jlimit(10, 22, r.getWidth() / 5));
    meter.setBounds(meters.withTrimmedBottom(row));
    r.removeFromLeft(2);
    gr.setBounds(r.removeFromLeft(juce::jlimit(5, 10, r.getWidth() / 10)).withTrimmedBottom(row));
    r.removeFromLeft(3);
    trim.setTextBoxStyle(juce::Slider::TextBoxBelow, false, r.getWidth(), row);
    trim.setBounds(r);
}

// ================================================================= LiveGroupStrip
LiveGroupStrip::LiveGroupStrip(EditorContext& c, int g, const LiveFlag& f)
    : ctx(c), proc(c.proc), index(g), live(f),
      trim(c.proc, ParamBank::index(Mod::Group, g, GroupP::Trim), juce::Slider::LinearVertical, 56),
      mute(c.proc, ParamBank::index(Mod::Group, g, GroupP::Mute), "M"),
      solo(c.proc, ParamBank::index(Mod::Group, g, GroupP::Solo), "S") {
    const juce::String n = "Group " + juce::String(g + 1);
    styleLiveLabel(name, 12.0f, true, palette().text, juce::Justification::centred);
    name.setEditable(false, true, false);
    name.setTooltip(n + ": double-click to rename");
    name.setTitle(n + " name");
    name.onTextChange = [this] { proc.getShow().group(index).name = name.getText().trim(); ctx.showModelChanged(); };
    styleLiveLabel(readout, 11.0f, false, palette().text, juce::Justification::centred);
    readout.setFont(monoFont(11.0f));
    readout.setTooltip(n + ": peak level (dBFS) / compressor gain reduction (dB)");
    auto& m = proc.getMeters();
    meterL.source = [this, &m] { return live.on ? m.groupPeakL[static_cast<size_t>(index)].getAndReset() : 0.0f; };
    meterR.source = [this, &m] { return live.on ? m.groupPeakR[static_cast<size_t>(index)].getAndReset() : 0.0f; };
    meterL.setTooltip(n + " left meter (click clears clip)");
    meterR.setTooltip(n + " right meter (click clears clip)");
    gr.source = [this, &m] { return live.on ? std::abs(m.groupGrDb[static_cast<size_t>(index)].load(std::memory_order_relaxed)) : gr.shownDb(); };
    trim.setTooltip(n + " fader / trim (-60..+24 dB)");
    mute.setTooltip(n + " mute");
    solo.setTooltip(n + " solo");
    for (juce::Component* comp : std::initializer_list<juce::Component*> { &name, &readout, &meterL, &meterR, &gr, &trim, &mute, &solo })
        addAndMakeVisible(comp);
    name.addMouseListener(this, false);
    setTitle(n + " strip");
    refreshNames();
}
LiveGroupStrip::~LiveGroupStrip() = default;

void LiveGroupStrip::refreshNames() { name.setText(proc.getShow().groupDisplayName(index), juce::dontSendNotification); }
void LiveGroupStrip::mouseDown(const juce::MouseEvent&) { select(ctx, Selection::Kind::Group, index); }

void LiveGroupStrip::tick() {
    const float db = juce::jmax(meterL.getDisplayedLevelDb(), meterR.getDisplayedLevelDb());
    readout.setText(formatDb(db) + " GR " + juce::String(gr.shownDb(), 1), juce::dontSendNotification);
    const bool sel = ctx.getSelection() == Selection { Selection::Kind::Group, index };
    if (sel != (getProperties()["sel"].toString() == "1")) { getProperties().set("sel", sel ? "1" : "0"); repaint(); }
}

void LiveGroupStrip::paint(juce::Graphics& g) {
    const auto& p = palette();
    const bool sel = ctx.getSelection() == Selection { Selection::Kind::Group, index };
    auto b = getLocalBounds().toFloat().reduced(1.5f);
    g.setColour(p.panel);
    g.fillRoundedRectangle(b, 5.0f);
    g.setColour(sel ? p.accent : p.panelEdge);
    g.drawRoundedRectangle(b, 5.0f, sel ? 2.5f : 1.0f);
    g.setColour(p.textMuted);
    g.setFont(font(10.0f, true));
    g.drawText((sel ? "SEL G" : "G") + juce::String(index + 1), getLocalBounds().reduced(5, 3).removeFromTop(scaled(11)), juce::Justification::topLeft);
}

void LiveGroupStrip::resized() {
    auto r = getLocalBounds().reduced(4);
    const int row = juce::jlimit(16, 24, r.getHeight() / 14);
    r.removeFromTop(scaled(11));
    name.setBounds(r.removeFromTop(row));
    auto tog = r.removeFromBottom(row + 4).reduced(0, 1);
    mute.setBounds(tog.removeFromLeft(tog.getWidth() / 2).reduced(1, 0));
    solo.setBounds(tog.reduced(1, 0));
    readout.setBounds(r.removeFromBottom(row - 2));
    r.removeFromTop(2);
    const int mw = juce::jlimit(5, 12, r.getWidth() / 9);
    meterL.setBounds(r.removeFromLeft(mw).withTrimmedBottom(row));
    r.removeFromLeft(1);
    meterR.setBounds(r.removeFromLeft(mw).withTrimmedBottom(row));
    r.removeFromLeft(2);
    gr.setBounds(r.removeFromLeft(juce::jmax(4, mw / 2 + 1)).withTrimmedBottom(row));
    r.removeFromLeft(2);
    trim.setTextBoxStyle(juce::Slider::TextBoxBelow, false, r.getWidth(), row);
    trim.setBounds(r);
}

// ================================================================= LiveMasterSection
LiveMasterSection::LiveMasterSection(EditorContext& c, const LiveFlag& f) : ctx(c), proc(c.proc), live(f) {
    header.setTooltip("Master crowd bus: click to edit it in the inspector");
    header.setTitle("Master bus");
    header.onClick = [this] { ctx.select({ Selection::Kind::Master, 0 }); };
    addAndMakeVisible(header);
    auto& m = proc.getMeters();
    history.snapshot = [this](float* mn, float* mx, float* rms, int b) { if (live.on) proc.getEngine().masterHistory().snapshot(mn, mx, rms, b); };
    history.setTitle("Master combined waveform");
    addAndMakeVisible(history);
    masterL.source = [this, &m] { return live.on ? m.masterPeakL.getAndReset() : 0.0f; };
    masterR.source = [this, &m] { return live.on ? m.masterPeakR.getAndReset() : 0.0f; };
    masterL.setTooltip("Master left (click clears clip)");
    masterR.setTooltip("Master right (click clears clip)");
    addAndMakeVisible(masterL);
    addAndMakeVisible(masterR);
    static const char* fxNames[kNumFx] = { "Delay", "Echo", "Tape", "Reverse", "Noise", "Reverb", "Multi" };
    for (int i = 0; i < kNumFx; ++i) {
        auto* mtr = fxMeters.add(new LevelMeter(LevelMeter::Orientation::Horizontal));
        mtr->source = [this, &m, i] { return live.on ? m.fxPeak[static_cast<size_t>(i)].getAndReset() : 0.0f; };
        mtr->setTooltip(juce::String(fxNames[i]) + " return activity");
        auto* l = fxLabels.add(new juce::Label({}, fxNames[i]));
        styleLiveLabel(*l, 11.0f, false, palette().textMuted);
        addAndMakeVisible(mtr);
        addAndMakeVisible(l);
    }
    limGr.source = [this, &m] { masterLimGr = std::abs(m.masterLimGrDb.load(std::memory_order_relaxed)); return masterLimGr; };
    outGr.source = [this, &m] {
        float worst = 0.0f;
        for (int o = 0; o < numOutputs; ++o) worst = juce::jmax(worst, std::abs(m.outLimGrDb[static_cast<size_t>(o)].load(std::memory_order_relaxed)));
        outLimGr = worst;
        return worst;
    };
    for (auto* l : { &limLabel, &outLimLabel }) { styleLiveLabel(*l, 11.0f, false, palette().text); l->setFont(monoFont(11.0f)); addAndMakeVisible(*l); }
    limLabel.setTooltip("Master limiter gain reduction");
    outLimLabel.setTooltip("Largest output-stage safety limiter gain reduction");
    addAndMakeVisible(limGr);
    addAndMakeVisible(outGr);
    setNumOutputs(proc.getActiveOutputChannelCount());
}
LiveMasterSection::~LiveMasterSection() = default;

void LiveMasterSection::setNumOutputs(int n) {
    n = juce::jlimit(0, kMaxOutputs, n);
    if (n == numOutputs) return;
    numOutputs = n;
    outMeters.clear();
    outLabels.clear();
    auto& m = proc.getMeters();
    for (int o = 0; o < n; ++o) {
        auto* mtr = outMeters.add(new LevelMeter());
        mtr->source = [this, &m, o] {
            if (!live.on) return 0.0f;
            float pk = m.outPeak[static_cast<size_t>(o)].getAndReset();
            if (m.outClip[static_cast<size_t>(o)].exchange(false, std::memory_order_relaxed)) pk = juce::jmax(pk, 1.0f);
            return pk;
        };
        mtr->setTooltip(proc.getShow().outputDisplayName(o) + " output meter (click clears clip)");
        mtr->setTitle(proc.getShow().outputDisplayName(o) + " meter");
        auto* l = outLabels.add(new juce::Label({}, juce::String(o + 1)));
        styleLiveLabel(*l, 10.5f, false, palette().textMuted, juce::Justification::centred);
        addAndMakeVisible(mtr);
        addAndMakeVisible(l);
    }
    resized();
}

void LiveMasterSection::tick() {
    if (!live.on) return;
    auto& m = proc.getMeters();
    intensity = juce::jlimit(0.0f, 1.0f, m.intensity.load(std::memory_order_relaxed));
    correlation = juce::jlimit(-1.0f, 1.0f, m.correlation.load(std::memory_order_relaxed));
    width = juce::jlimit(0.0f, 1.0f, m.width.load(std::memory_order_relaxed));
    limLabel.setText("Master lim GR " + juce::String(masterLimGr, 1) + " dB", juce::dontSendNotification);
    outLimLabel.setText("Output lim GR " + juce::String(outLimGr, 1) + " dB", juce::dontSendNotification);
    repaint(intensityArea);
    repaint(corrArea);
}

void LiveMasterSection::paint(juce::Graphics& g) {
    const auto& p = palette();
    auto b = getLocalBounds().toFloat().reduced(1.5f);
    g.setColour(p.panel);
    g.fillRoundedRectangle(b, 5.0f);
    g.setColour(p.panelEdge);
    g.drawRoundedRectangle(b, 5.0f, 1.0f);

    const auto bar = [&](juce::Rectangle<int> area, const juce::String& label, const juce::String& value) {
        auto top = area.removeFromTop(area.getHeight() / 2);
        g.setColour(p.textMuted);
        g.setFont(font(11.0f));
        g.drawText(label, top, juce::Justification::centredLeft);
        g.setColour(p.text);
        g.setFont(monoFont(11.5f));
        g.drawText(value, top, juce::Justification::centredRight);
        g.setColour(p.background.darker(0.3f));
        g.fillRoundedRectangle(area.toFloat().reduced(0, 1), 2.0f);
        return area.toFloat().reduced(1.5f, 2.5f);
    };
    {   // crowd intensity 0..1
        auto r = bar(intensityArea, "Crowd intensity", juce::String(juce::roundToInt(intensity * 100.0f)) + " %");
        g.setColour(p.accent);
        g.fillRect(r.withWidth(r.getWidth() * intensity));
    }
    {   // correlation -1..+1 with centre marker, width as text
        const juce::String corrText = (correlation >= 0 ? "+" : "") + juce::String(correlation, 2);
        auto r = bar(corrArea, "Corr / width", corrText + "  W" + juce::String(juce::roundToInt(width * 100.0f)) + "%");
        const float cx = r.getCentreX();
        const float x = r.getX() + (correlation + 1.0f) * 0.5f * r.getWidth();
        g.setColour(correlation < 0.0f ? p.warn : p.ok);
        g.fillRect(juce::jmin(cx, x), r.getY(), std::abs(x - cx), r.getHeight());
        g.setColour(p.textMuted);
        g.fillRect(cx - 0.5f, r.getY() - 1.0f, 1.0f, r.getHeight() + 2.0f);
        g.setFont(font(9.5f));
        g.drawText("-1", r.toNearestInt(), juce::Justification::centredLeft);
        g.drawText("+1", r.toNearestInt(), juce::Justification::centredRight);
    }
    g.setColour(p.textMuted);
    g.setFont(font(10.5f, true));
    if (!fxArea.isEmpty()) g.drawText("FX RETURN ACTIVITY", fxArea.withHeight(scaled(13)), juce::Justification::centredLeft);
    if (!limArea.isEmpty()) g.drawText("LIMITERS", limArea.withHeight(scaled(13)), juce::Justification::centredLeft);
    if (!outArea.isEmpty()) g.drawText("OUTPUTS (" + juce::String(numOutputs) + ")", outArea.withHeight(scaled(13)), juce::Justification::centredLeft);
}

void LiveMasterSection::resized() {
    auto r = getLocalBounds().reduced(6);
    const int h = r.getHeight();
    const int row = juce::jlimit(14, 22, h / 28);
    header.setBounds(r.removeFromTop(row + 8));
    r.removeFromTop(4);
    intensityArea = r.removeFromTop(row * 2);
    r.removeFromTop(3);
    corrArea = r.removeFromTop(row * 2);
    r.removeFromTop(4);
    auto hist = r.removeFromTop(juce::jlimit(24, 90, h / 9));
    masterR.setBounds(hist.removeFromRight(8));
    hist.removeFromRight(2);
    masterL.setBounds(hist.removeFromRight(8));
    hist.removeFromRight(3);
    history.setBounds(hist);
    r.removeFromTop(5);
    fxArea = r.removeFromTop(scaled(13) + kNumFx * (row - 1) + 2);
    {
        auto a = fxArea.withTrimmedTop(scaled(13) + 2);
        const int lw = juce::jmin(64, a.getWidth() / 3);
        for (int i = 0; i < kNumFx; ++i) {
            auto line = a.removeFromTop(row - 1);
            fxLabels[i]->setBounds(line.removeFromLeft(lw));
            fxMeters[i]->setBounds(line.reduced(0, 2));
        }
    }
    r.removeFromTop(5);
    limArea = r.removeFromTop(scaled(13) + 2 * (row + 2) + 2);
    {
        auto a = limArea.withTrimmedTop(scaled(13) + 2);
        auto l1 = a.removeFromTop(row + 2);
        limGr.setBounds(l1.removeFromRight(juce::jmax(30, l1.getWidth() / 3)).reduced(0, 3));
        limLabel.setBounds(l1);
        auto l2 = a.removeFromTop(row + 2);
        outGr.setBounds(l2.removeFromRight(juce::jmax(30, l2.getWidth() / 3)).reduced(0, 3));
        outLimLabel.setBounds(l2);
    }
    r.removeFromTop(5);
    outArea = r;
    auto a = r.withTrimmedTop(scaled(13) + 2);
    if (numOutputs > 0) {
        // One or two rows depending on the room available.
        const int rows = (a.getWidth() / numOutputs < 12 && a.getHeight() > 120) ? 2 : 1;
        const int perRow = (numOutputs + rows - 1) / rows;
        const int cw = a.getWidth() / juce::jmax(1, perRow);
        const int rh = a.getHeight() / rows;
        for (int o = 0; o < numOutputs; ++o) {
            auto cell = juce::Rectangle<int>(a.getX() + (o % perRow) * cw, a.getY() + (o / perRow) * rh, cw, rh).reduced(juce::jmin(4, cw / 6), 1);
            outLabels[o]->setBounds(cell.removeFromBottom(row));
            outMeters[o]->setBounds(cell.withSizeKeepingCentre(juce::jmin(cell.getWidth(), 18), cell.getHeight()));
        }
    }
}
}
