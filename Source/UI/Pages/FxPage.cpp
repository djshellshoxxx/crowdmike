// FX page (GUI_SPEC sections 6 and 10): a rack of the seven send/return effects (Digital Delay, Echo Throw, Tape Delay,
// Reverse Chunk with its dedicated buffer display, Noise Exciter, Stadium Reverb with freeze / tail kill, Crowd Multiplier) followed by insert-module cards (De-esser,
// Compressor, Limiter, HPF/LPF, Parametric/Notch EQ, Width/Decorrelation, Gate/Expander) that act on the target
// chosen at the top (an input, a group or the master bus). Every card: bypass/on, wet/dry or mix where it exists,
// meters where the engine provides them, reset-to-defaults (one undo step), presets, MIDI/macro assignment shortcut.
// Send cards can be reordered (drag handle or Up/Down on the focused handle); this changes the DISPLAY order only.
#include "UI/Pages.h"
#include "UI/EditKit.h"
#include "UI/Meters.h"
#include "UI/ReverseViz.h"
#include "UI/Widgets.h"

namespace crowdmike::ui {
using namespace crowdmike::params;

namespace {
struct SendFxInfo {
    Mod mod;
    const char* title;
    const char* subtitle;
    const char* presetTarget;
    int bypass, mix, ret;
    const char* mixCaption;
    const char* toggleText = "BYPASS";   // the legacy FX have a Bypass parameter; slots 6 and 7 have an On parameter
};
const SendFxInfo& sendInfo(int f) {
    static const SendFxInfo info[kNumFx] = {
        { Mod::FxDelay, "Digital Delay / Echo", "single / ping-pong / multi-tap, tempo sync", "delay", DelayP::Bypass, DelayP::Mix, DelayP::Return, "Wet/Dry" },
        { Mod::FxEcho, "Echo Throw", "momentary capture + repeats", "echo", EchoP::Bypass, EchoP::Send, EchoP::Return, "Send" },
        { Mod::FxTape, "Tape Delay", "wow / flutter / drive / age", "tape", TapeP::Bypass, TapeP::Mix, TapeP::Return, "Wet/Dry" },
        { Mod::FxReverse, "Reverse Chunk", "plays captured chunks backward", "reverse", ReverseP::Bypass, ReverseP::Mix, ReverseP::Return, "Wet/Dry" },
        { Mod::FxNoise, "Noise Exciter", "shaped noise under the crowd (capped -24 dBFS)", "noise", NoiseP::Bypass, NoiseP::Amount, NoiseP::Return, "Amount" },
        { Mod::FxReverb, "Stadium Reverb", "8-line FDN, freeze / tail kill, tempo-synced pre-delay", "reverb", ReverbP::On, ReverbP::Wet, ReverbP::Return, "Wet", "ON" },
        { Mod::FxMulti, "Crowd Multiplier", "2-16 decorrelated voices make a small crowd sound larger", "multi", MultiP::On, MultiP::Level, MultiP::Return, "Level", "ON" },
    };
    return info[juce::jlimit(0, kNumFx - 1, f)];
}
int fxIndex(int f, int e) { return ParamBank::index(sendInfo(f).mod, 0, e); }

std::unique_ptr<juce::Component> slider(CrowdMikeAudioProcessor& p, int idx, int textW = 54) {
    return std::make_unique<ParamSlider>(p, idx, juce::Slider::LinearHorizontal, textW);
}
const juce::String kReorderTip = "Drag (or focus and press Up/Down) to reorder the rack display. "
                                 "Display order only: the DSP topology (parallel send/return effects) does not change.";

// ---------------------------------------------------------------- drag handle
class DragHandle final : public juce::Component, public juce::SettableTooltipClient {
public:
    std::function<void(int delta)> onMove;
    std::function<void(int yInParent)> onDrag;
    std::function<void()> onDrop;
    DragHandle() { setWantsKeyboardFocus(true); setMouseCursor(juce::MouseCursor::DraggingHandCursor); setTooltip(kReorderTip); setTitle("Reorder handle"); }
    void paint(juce::Graphics& g) override {
        const auto& p = palette();
        g.setColour(hasKeyboardFocus(false) ? p.accent : p.textMuted);
        const auto b = getLocalBounds().toFloat().reduced(4.0f, 6.0f);
        for (int k = 0; k < 3; ++k) g.fillRect(b.getX(), b.getY() + b.getHeight() * (0.2f + 0.3f * static_cast<float>(k)) - 1.0f, b.getWidth(), 2.0f);
    }
    void mouseDrag(const juce::MouseEvent& e) override { if (onDrag) onDrag(e.getEventRelativeTo(getParentComponent()->getParentComponent()).y); }
    void mouseUp(const juce::MouseEvent& e) override { if (e.getDistanceFromDragStart() > 3 && onDrop) onDrop(); }
    bool keyPressed(const juce::KeyPress& k) override {
        if (k.getKeyCode() == juce::KeyPress::upKey) { if (onMove) onMove(-1); return true; }
        if (k.getKeyCode() == juce::KeyPress::downKey) { if (onMove) onMove(1); return true; }
        return false;
    }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
};

// ---------------------------------------------------------------- shared card header
class ModuleCard : public kit::Card {
public:
    ModuleCard(CrowdMikeAudioProcessor& p, const juce::String& t) : kit::Card(t), proc(p), panel(p) {
        reset.setTooltip("Reset every parameter of this module to its default (one undo step)");
        presets.setTooltip("Factory presets, save / load user presets");
        assign.setTooltip("MIDI learn / macro assignment for a parameter of this module (right-click any control also works)");
        for (auto* b : { &reset, &presets, &assign }) addAndMakeVisible(*b);
        addAndMakeVisible(panel);
        reset.onClick = [this] { kit::resetToDefaults(proc, allParams, "Reset " + title); };
        assign.onClick = [this] { kit::showAssignMenu(proc, &assign, allParams); };
    }
    std::vector<int> allParams;
    juce::TextButton reset { "Reset" }, presets { "Presets" }, assign { "MIDI/Macro" };
    ParamPanel panel;
protected:
    CrowdMikeAudioProcessor& proc;
    // header row: [extra controls ...] .... [Reset][Presets][MIDI/Macro]
    juce::Rectangle<int> layoutHeader(juce::Rectangle<int>& r) {
        auto row = r.removeFromTop(px(28));
        assign.setBounds(row.removeFromRight(px(92)).reduced(px(2)));
        presets.setBounds(row.removeFromRight(px(70)).reduced(px(2)));
        reset.setBounds(row.removeFromRight(px(58)).reduced(px(2)));
        r.removeFromTop(px(4));
        return row;
    }
};

// ---------------------------------------------------------------- send/return effect card
class SendCard final : public ModuleCard {
public:
    SendCard(EditorContext& c, int fx) : ModuleCard(c.proc, sendInfo(fx).title), ctx(c), f(fx),
        bypass(c.proc, fxIndex(fx, sendInfo(fx).bypass), sendInfo(fx).toggleText),
        mix(sendInfo(fx).mixCaption, slider(c.proc, fxIndex(fx, sendInfo(fx).mix), 46), 50),
        ret("Return", slider(c.proc, fxIndex(fx, sendInfo(fx).ret), 56), 44),
        meter(LevelMeter::Orientation::Horizontal) {
        const auto& info = sendInfo(f);
        setComponentID("fx-card-" + juce::String(f));
        setSubtitle(info.subtitle);
        allParams = paramIndices(info.mod, 0);
        bypass.setTooltip(juce::String(info.toggleText) == "ON" ? "Effect on / off (click-free fade; the effect costs nothing while off)" : "Bypass (click-free crossfade)");
        mix.setTooltip(juce::String(info.mixCaption) + " of the effect");
        ret.setTooltip("Return level into the master bus (dB)");
        meter.source = [this] { return live ? proc.getMeters().fxPeak[static_cast<size_t>(f)].getAndReset() : 0.0f; };
        meter.setTooltip("Effect return level (output meter). Input send levels are set per input/group.");
        reset.setComponentID("fx-reset-" + juce::String(f));
        presets.onClick = [this, info] {
            kit::PresetTarget t;
            t.module = info.mod; t.factoryTarget = info.presetTarget; t.folderName = info.presetTarget; t.title = sendInfo(f).title;
            kit::showPresetMenu(proc, &presets, t);
        };
        handle.setComponentID("fx-handle-" + juce::String(f));
        for (juce::Component* comp : std::initializer_list<juce::Component*> { &handle, &bypass, &mix, &ret, &meter }) addAndMakeVisible(comp);

        std::vector<int> body;
        std::vector<int> skip { info.bypass, info.mix, info.ret };
        if (info.mod == Mod::FxEcho) {
            throwBtn = std::make_unique<MomentaryButton>(proc, fxIndex(f, EchoP::Trigger), "THROW");
            tailBtn = std::make_unique<MomentaryButton>(proc, fxIndex(f, EchoP::TailKill), "TAIL KILL");
            throwBtn->setTooltip("Echo throw: opens the send for the capture length while held (key E)");
            tailBtn->setTooltip("Tail kill: fades the echo loop out while held");
            throwBtn->setColour(juce::TextButton::buttonColourId, palette().accentDim);
            tailBtn->setColour(juce::TextButton::buttonColourId, palette().danger.darker(0.5f));
            addAndMakeVisible(*throwBtn);
            addAndMakeVisible(*tailBtn);
            skip.push_back(EchoP::Trigger); skip.push_back(EchoP::TailKill);
        }
        if (info.mod == Mod::FxReverb) {
            freezeBtn = std::make_unique<ParamToggle>(proc, fxIndex(f, ReverbP::Freeze), "FREEZE");
            tailBtn = std::make_unique<MomentaryButton>(proc, fxIndex(f, ReverbP::TailKill), "TAIL KILL");
            freezeBtn->setTooltip("Freeze: closes the input and holds the current tail (also follows the global Freeze / Hold)");
            tailBtn->setTooltip("Tail kill: fades the reverb out in about 40 ms while held (key K)");
            tailBtn->setColour(juce::TextButton::buttonColourId, palette().danger.darker(0.5f));
            addAndMakeVisible(*freezeBtn);
            addAndMakeVisible(*tailBtn);
            skip.push_back(ReverbP::Freeze); skip.push_back(ReverbP::TailKill);
        }
        if (info.mod == Mod::FxReverse) {
            viz = std::make_unique<ReverseVizView>();
            viz->setComponentID("reverse-viz");
            viz->source = [this] { return proc.getEngine().getReverseViz(); };
            viz->chunkSeconds = [this] { return kit::actual(proc, fxIndex(f, ReverseP::ChunkSec)); };
            viz->active = [this] { return live; };
            addAndMakeVisible(*viz);
            trigger = std::make_unique<MomentaryButton>(proc, fxIndex(f, ReverseP::Trigger), "TRIGGER");
            trigger->setTooltip("Reverse trigger: freezes the most recent chunk and plays it backward (key V)");
            trigger->setColour(juce::TextButton::buttonColourId, palette().accentDim);
            addAndMakeVisible(*trigger);
            for (int e : { ReverseP::ChunkSec, ReverseP::Mode, ReverseP::TrigMode, ReverseP::Retrigger }) {
                auto* cell = vizCells.add(new ParamCell(proc, fxIndex(f, e)));
                addAndMakeVisible(cell);
                skip.push_back(e);
            }
            skip.push_back(ReverseP::Trigger);
        }
        for (int e = 0; e < ParamBank::info(info.mod).defCount; ++e)
            if (std::find(skip.begin(), skip.end(), e) == skip.end()) body.push_back(fxIndex(f, e));
        panel.setParams(body);
        panel.setCellSize(176, 42);
        setTooltip(kReorderTip);
    }
    void setTooltip(const juce::String& t) { handle.setTooltip(t); }
    void setLive(bool l) { live = l; }
    void mouseDown(const juce::MouseEvent&) override { ctx.select({ Selection::Kind::Fx, f }); }
    int layoutHeight(int w) {
        int h = titleHeight() + px(12) + px(32);
        if (throwBtn || freezeBtn) h += px(52);
        if (viz) h += px(130);
        return h + panel.getPreferredHeight(w - px(12));
    }
    void resized() override {
        auto r = getContentBounds();
        auto row = layoutHeader(r);
        handle.setBounds(row.removeFromLeft(px(24)));
        row.removeFromLeft(px(4));
        bypass.setBounds(row.removeFromLeft(px(92)).reduced(px(2)));
        meter.setBounds(row.removeFromRight(px(90)).reduced(px(4), px(8)));
        const int half = row.getWidth() / 2;
        mix.setBounds(row.removeFromLeft(juce::jmin(half, px(220))).reduced(px(2)));
        ret.setBounds(row.removeFromLeft(juce::jmin(half, px(230))).reduced(px(2)));
        if (throwBtn || freezeBtn) {
            auto big = r.removeFromTop(px(48));
            juce::Component& first = throwBtn ? static_cast<juce::Component&>(*throwBtn) : static_cast<juce::Component&>(*freezeBtn);
            first.setBounds(big.removeFromLeft(big.getWidth() / 2).reduced(px(4), px(2)));
            tailBtn->setBounds(big.reduced(px(4), px(2)));
            r.removeFromTop(px(4));
        }
        if (viz) {
            auto area = r.removeFromTop(px(126));
            auto ctl = area.removeFromRight(juce::jmax(px(240), area.getWidth() / 3));
            viz->setBounds(area.reduced(px(2)));
            trigger->setBounds(ctl.removeFromTop(px(36)).reduced(px(4), px(2)));
            const int cw = ctl.getWidth() / 2, ch = ctl.getHeight() / 2;
            for (int k = 0; k < vizCells.size(); ++k)
                vizCells[k]->setBounds(ctl.getX() + (k % 2) * cw, ctl.getY() + (k / 2) * ch, cw, ch);
            r.removeFromTop(px(4));
        }
        panel.setBounds(r);
    }
    DragHandle handle;
    std::unique_ptr<MomentaryButton> throwBtn, tailBtn, trigger;
    std::unique_ptr<ParamToggle> freezeBtn;
    std::unique_ptr<ReverseVizView> viz;
private:
    EditorContext& ctx;
    const int f;
    bool live = false;
    ParamToggle bypass;
    kit::Captioned mix, ret;
    LevelMeter meter;
    juce::OwnedArray<ParamCell> vizCells;
};

// ---------------------------------------------------------------- insert modules
struct FxTarget { Selection::Kind kind = Selection::Kind::Input; int index = 0; };

struct InsertSpec {
    juce::String title, subtitle, unavailable;
    Mod mod = Mod::Input;
    int instance = 0;
    std::vector<int> params;       // global indices (all parameters of the block)
    int onIndex = -1, mixIndex = -1;
    std::function<float()> gr;     // positive dB of reduction, null when not metered
    kit::PresetTarget preset;
};

InsertSpec makeInsert(CrowdMikeAudioProcessor& proc, int kind, FxTarget t) {
    InsertSpec s;
    const bool in = t.kind == Selection::Kind::Input, grp = t.kind == Selection::Kind::Group;
    s.mod = in ? Mod::Input : (grp ? Mod::Group : Mod::Master);
    s.instance = in || grp ? t.index : 0;
    const int i = s.instance;
    auto& m = proc.getMeters();
    const auto base = [&](int e) { return ParamBank::index(s.mod, i, e); };
    s.preset.module = s.mod;
    s.preset.instance = i;
    s.preset.factoryTarget = in ? "input" : (grp ? "group" : "master");
    switch (kind) {
        case 0:
            s.title = "De-esser"; s.subtitle = "split-band sibilance / whistle control";
            if (!in) { s.unavailable = "Not in the group/master chain (EFFECTS_SPEC 18: the de-esser is an input insert). Select an input."; break; }
            s.params = paramRange(Mod::Input, i, InputP::DeEssOn, DeEssP::Count); s.onIndex = base(InputP::DeEssOn);
            s.gr = [&proc, i] { return std::abs(proc.getEngine().inputChannelForMeters(i).getDeEssGrDb()); };
            s.preset.blockPrefixes = { "deess" };
            break;
        case 1:
            s.title = "Compressor"; s.subtitle = "threshold / ratio / knee / makeup / parallel mix";
            if (in) { s.params = paramRange(Mod::Input, i, InputP::InCompOn, CompP::Count); s.onIndex = base(InputP::InCompOn); s.mixIndex = base(InputP::InCompMix);
                      s.gr = [&m, i] { return std::abs(m.inGrDb[static_cast<size_t>(i)].load()); }; }
            else if (grp) { s.params = paramRange(Mod::Group, i, GroupP::GrpCompOn, CompP::Count); s.onIndex = base(GroupP::GrpCompOn); s.mixIndex = base(GroupP::GrpCompMix);
                            s.gr = [&m, i] { return std::abs(m.groupGrDb[static_cast<size_t>(i)].load()); }; }
            else { s.params = paramRange(Mod::Master, 0, MasterP::MstCompOn, CompP::Count); s.onIndex = base(MasterP::MstCompOn); s.mixIndex = base(MasterP::MstCompMix);
                   s.gr = [&m] { return std::abs(m.masterGrDb.load()); }; }
            s.preset.blockPrefixes = { "comp" };
            break;
        case 2:
            s.title = "Limiter"; s.subtitle = "ceiling / release / input gain";
            if (in) { s.unavailable = "Inputs have no limiter (use the compressor). Output limiters are always on unless unlocked in Settings."; break; }
            if (grp) { s.params = paramRange(Mod::Group, i, GroupP::GrpLimOn, LimP::Count); s.onIndex = base(GroupP::GrpLimOn); }
            else { s.params = paramRange(Mod::Master, 0, MasterP::MstLimOn, LimP::Count); s.onIndex = base(MasterP::MstLimOn);
                   s.gr = [&m] { return std::abs(m.masterLimGrDb.load()); }; }
            s.preset.blockPrefixes = { "lim" };
            break;
        case 3:
            s.title = "HPF / LPF"; s.subtitle = "cascaded Butterworth, per-filter enable";
            if (in) { s.params = paramRange(Mod::Input, i, InputP::HpOn, 6); s.preset.blockPrefixes = { "highPass", "lowPass" }; }
            else if (grp) { s.params = paramRange(Mod::Group, i, GroupP::HpOn, 4); s.preset.blockPrefixes = { "hp", "lp" }; }
            else s.unavailable = "The master bus has no HPF/LPF: use the master EQ shelves or the per-output filters.";
            break;
        case 4:
            s.title = "Parametric / Notch EQ"; s.subtitle = "bell / notch / shelves, per-band enable";
            if (in) { s.params = paramRange(Mod::Input, i, InputP::NotchOn, 4);
                      const auto eq = paramRange(Mod::Input, i, InputP::Eq1On, 20); s.params.insert(s.params.end(), eq.begin(), eq.end());
                      s.preset.blockPrefixes = { "notch", "eq" }; }
            else if (grp) { s.params = paramRange(Mod::Group, i, GroupP::Eq1On, 20); s.preset.blockPrefixes = { "eq" }; }
            else { s.params = paramRange(Mod::Master, 0, MasterP::EqOn, 41); s.onIndex = base(MasterP::EqOn); s.preset.blockPrefixes = { "eq" }; }
            break;
        case 5:
            s.title = "Width / Decorrelation"; s.subtitle = "M/S width, decorrelation, safe Haas, LF mono";
            if (in) { s.unavailable = "Inputs are mono (or linked pairs): width is set on groups and the master bus."; break; }
            if (grp) { s.params = { base(GroupP::Width) }; s.preset.blockPrefixes = { "width" }; }
            else { s.params = paramRange(Mod::Master, 0, MasterP::WidthOn, WidthP::Count); s.onIndex = base(MasterP::WidthOn);
                   s.preset.blockPrefixes = { "width", "decorr", "haas", "monoCompat", "lfMono" }; }
            break;
        default:
            s.title = "Gate / Expander"; s.subtitle = "threshold / range / hold / hysteresis, sidechain filters";
            if (!in) { s.unavailable = "The gate is an input insert (stage-spill control). Select an input."; break; }
            s.params = paramRange(Mod::Input, i, InputP::GateOn, GateP::Count); s.onIndex = base(InputP::GateOn);
            s.gr = [&proc, i] { return std::abs(proc.getEngine().inputChannelForMeters(i).getGateGrDb()); };
            s.preset.blockPrefixes = { "gate" };
            break;
    }
    s.preset.folderName = s.preset.factoryTarget + "-" + s.title.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ").toLowerCase();
    s.preset.title = s.title + " presets";
    return s;
}
constexpr int kNumInserts = 7;

class InsertCard final : public ModuleCard {
public:
    InsertCard(CrowdMikeAudioProcessor& p, InsertSpec specIn, int kindIndex) : ModuleCard(p, specIn.title), spec(std::move(specIn)), grMeter(18.0f) {
        setComponentID("insert-card-" + juce::String(kindIndex));
        setSubtitle(spec.unavailable.isEmpty() ? spec.subtitle : juce::String("not available on this target"));
        allParams = spec.params;
        reset.setComponentID("insert-reset-" + juce::String(kindIndex));
        if (spec.unavailable.isNotEmpty()) {
            kit::styleLabel(note, 12.0f, false, palette().textMuted, juce::Justification::topLeft);
            note.setText(juce::String::fromUTF8("\xe2\x93\x98 ") + spec.unavailable, juce::dontSendNotification);
            addAndMakeVisible(note);
            for (auto* b : { &reset, &presets, &assign }) b->setVisible(false);
            panel.setVisible(false);
            return;
        }
        if (spec.onIndex >= 0) { onToggle = std::make_unique<ParamToggle>(proc, spec.onIndex, "ON"); onToggle->setTooltip("Module on / bypassed (click-free)"); addAndMakeVisible(*onToggle); }
        else { kit::styleLabel(note, 11.5f, false, palette().textMuted); note.setText("per-band / per-filter enable", juce::dontSendNotification); addAndMakeVisible(note); }
        if (spec.mixIndex >= 0) { mix = std::make_unique<kit::Captioned>("Mix", slider(proc, spec.mixIndex, 46), 28); mix->setTooltip("Wet/dry (parallel) mix"); addAndMakeVisible(*mix); }
        else { kit::styleLabel(mixNote, 11.5f, false, palette().textMuted); mixNote.setText("100% wet (inline insert)", juce::dontSendNotification); addAndMakeVisible(mixNote); }
        if (spec.gr) { grMeter.source = [this] { return live ? spec.gr() : 0.0f; }; addAndMakeVisible(grMeter); kit::styleLabel(grLabel, 11.0f, false, palette().textMuted); addAndMakeVisible(grLabel); }
        presets.onClick = [this] { kit::showPresetMenu(proc, &presets, spec.preset); };
        std::vector<int> body;
        for (int idx : spec.params) if (idx != spec.onIndex && idx != spec.mixIndex) body.push_back(idx);
        panel.setParams(body);
        panel.setCellSize(176, 42);
    }
    void setLive(bool l) { live = l; }
    void tick() { if (spec.gr && live) grLabel.setText("GR " + juce::String(spec.gr(), 1) + " dB", juce::dontSendNotification); }
    int layoutHeight(int w) {
        if (spec.unavailable.isNotEmpty()) return titleHeight() + px(46);
        return titleHeight() + px(12) + px(32) + panel.getPreferredHeight(w - px(12));
    }
    void resized() override {
        auto r = getContentBounds();
        if (spec.unavailable.isNotEmpty()) { note.setBounds(r); return; }
        auto row = layoutHeader(r);
        if (onToggle) onToggle->setBounds(row.removeFromLeft(px(70)).reduced(px(2)));
        else note.setBounds(row.removeFromLeft(px(170)));
        if (mix) mix->setBounds(row.removeFromLeft(px(200)).reduced(px(2)));
        else mixNote.setBounds(row.removeFromLeft(px(150)));
        if (spec.gr) {
            grLabel.setBounds(row.removeFromRight(px(84)));
            grMeter.setBounds(row.removeFromRight(px(80)).reduced(px(2), px(8)));
        }
        panel.setBounds(r);
    }
    const InsertSpec& getSpec() const noexcept { return spec; }
private:
    InsertSpec spec;
    bool live = false;
    std::unique_ptr<ParamToggle> onToggle;
    std::unique_ptr<kit::Captioned> mix;
    juce::Label note, mixNote, grLabel;
    GainReductionMeter grMeter;
};

// ---------------------------------------------------------------- page
class FxPage final : public Page, private Tickable {
public:
    explicit FxPage(EditorContext& c) : Page(c), targetMeter(LevelMeter::Orientation::Horizontal) {
        kit::styleLabel(title, 16.0f, true, palette().accent);
        title.setText("FX rack", juce::dontSendNotification);
        addAndMakeVisible(title);
        targetBox.setComponentID("fx-target");
        targetBox.setTitle("Insert target");
        targetBox.setTooltip("Insert modules below operate on this input, group or the master bus (follows the selection)");
        targetBox.onChange = [this] { targetChanged(); };
        addAndMakeVisible(targetBox);
        kit::styleLabel(targetLabel, 12.0f, false, palette().textMuted);
        targetLabel.setText("Insert target:", juce::dontSendNotification);
        addAndMakeVisible(targetLabel);
        targetMeter.source = [this] { return live ? targetPeak() : 0.0f; };
        targetMeter.setTooltip("Level of the insert target");
        addAndMakeVisible(targetMeter);
        for (auto* l : { &sendHeader, &insertHeader }) { kit::styleLabel(*l, 13.0f, true, palette().accent); content.addAndMakeVisible(*l); }
        sendHeader.setText("SEND / RETURN EFFECTS  (display order is adjustable; the effects always run in parallel on the FX sends)", juce::dontSendNotification);
        for (int f = 0; f < kNumFx; ++f) {
            auto* card = sendCards.add(new SendCard(c, f));
            card->handle.onMove = [this, f](int d) { moveFx(f, positionOf(f) + d); };
            card->handle.onDrag = [this, f](int y) { dragFx = f; dragY = y; dropIndicator(); };
            card->handle.onDrop = [this] { finishDrag(); };
            content.addAndMakeVisible(card);
        }
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);
        fillTargets();
        targetChanged();
    }
    void pageShown() override {
        live = true;
        if (proc.getActiveInputChannelCount() != lastInputs) fillTargets();
        followSelection();
        for (auto* c : sendCards) c->setLive(true);
        for (auto* c : insertCards) c->setLive(true);
        layoutContent();
    }
    void pageHidden() override {
        live = false;
        for (auto* c : sendCards) c->setLive(false);
        for (auto* c : insertCards) c->setLive(false);
    }
    void tick() override { if (live) for (auto* c : insertCards) c->tick(); }
    void resized() override {
        auto r = getLocalBounds().reduced(px(8));
        auto top = r.removeFromTop(px(28));
        title.setBounds(top.removeFromLeft(px(120)));
        targetLabel.setBounds(top.removeFromLeft(px(90)));
        targetBox.setBounds(top.removeFromLeft(px(220)).reduced(0, px(1)));
        top.removeFromLeft(px(8));
        targetMeter.setBounds(top.removeFromLeft(px(160)).reduced(0, px(8)));
        r.removeFromTop(px(4));
        viewport.setBounds(r);
        layoutContent();
    }

    // Display-order API (also used by tests).
    int positionOf(int f) const {
        const auto& order = proc.getShow().ui.fxOrder;
        for (int k = 0; k < kNumFx; ++k) if (order[static_cast<size_t>(k)] == f) return k;
        return f;
    }
    void moveFx(int f, int newPos) {
        auto& order = proc.getShow().ui.fxOrder;
        newPos = juce::jlimit(0, kNumFx - 1, newPos);
        const int cur = positionOf(f);
        if (cur == newPos) return;
        std::vector<int> v(order.begin(), order.end());
        v.erase(v.begin() + cur);
        v.insert(v.begin() + newPos, f);
        for (int k = 0; k < kNumFx; ++k) order[static_cast<size_t>(k)] = v[static_cast<size_t>(k)];
        layoutContent();
        sendCards[f]->handle.grabKeyboardFocus();
        ctx.say(juce::String(sendInfo(f).title) + " moved to rack slot " + juce::String(newPos + 1) + " (display order only)");
    }

private:
    void selectionChanged() override { followSelection(); }
    void showModelChanged() override { if (proc.getActiveInputChannelCount() != lastInputs) fillTargets(); layoutContent(); }

    float targetPeak() {
        auto& m = proc.getMeters();
        if (target.kind == Selection::Kind::Input) return m.inPeak[static_cast<size_t>(target.index)].getAndReset();
        if (target.kind == Selection::Kind::Group) return juce::jmax(m.groupPeakL[static_cast<size_t>(target.index)].getAndReset(), m.groupPeakR[static_cast<size_t>(target.index)].getAndReset());
        return juce::jmax(m.masterPeakL.getAndReset(), m.masterPeakR.getAndReset());
    }
    void fillTargets() {
        lastInputs = proc.getActiveInputChannelCount();
        const auto& show = proc.getShow();
        targetBox.clear(juce::dontSendNotification);
        for (int i = 0; i < lastInputs; ++i) targetBox.addItem("Input " + juce::String(i + 1) + ": " + show.inputDisplayName(i), 1 + i);
        targetBox.addSeparator();
        for (int g = 0; g < kMaxGroups; ++g) targetBox.addItem("Group " + juce::String(g + 1) + ": " + show.groupDisplayName(g), 100 + g);
        targetBox.addSeparator();
        targetBox.addItem("Master crowd bus", 200);
        targetBox.setSelectedId(idFor(target), juce::dontSendNotification);
        if (targetBox.getSelectedId() == 0) targetBox.setSelectedId(1, juce::dontSendNotification);
    }
    static int idFor(FxTarget t) { return t.kind == Selection::Kind::Input ? 1 + t.index : (t.kind == Selection::Kind::Group ? 100 + t.index : 200); }
    void followSelection() {
        const auto s = ctx.getSelection();
        if (s.kind == Selection::Kind::Input || s.kind == Selection::Kind::Group || s.kind == Selection::Kind::Master) {
            const FxTarget t { s.kind, s.kind == Selection::Kind::Master ? 0 : s.index };
            if (t.kind != target.kind || t.index != target.index) { targetBox.setSelectedId(idFor(t), juce::dontSendNotification); targetChanged(); }
        }
    }
    void targetChanged() {
        const int id = targetBox.getSelectedId();
        if (id >= 200) target = { Selection::Kind::Master, 0 };
        else if (id >= 100) target = { Selection::Kind::Group, id - 100 };
        else target = { Selection::Kind::Input, juce::jmax(0, id - 1) };
        insertCards.clear();
        for (int k = 0; k < kNumInserts; ++k) {
            auto* card = insertCards.add(new InsertCard(proc, makeInsert(proc, k, target), k));
            card->setLive(live);
            content.addAndMakeVisible(card);
        }
        insertHeader.setText("INSERT MODULES ON: " + targetBox.getText().toUpperCase(), juce::dontSendNotification);
        layoutContent();
    }
    void layoutContent() {
        const int w = viewport.getWidth() - viewport.getScrollBarThickness() - 2;
        if (w <= 0) return;
        int y = 0;
        sendHeader.setBounds(0, y, w, px(22)); y += px(24);
        const auto& order = proc.getShow().ui.fxOrder;
        for (int k = 0; k < kNumFx; ++k) {
            auto* card = sendCards[order[static_cast<size_t>(k)]];
            const int h = card->layoutHeight(w);
            card->setBounds(0, y, w, h);
            card->setHighlighted(dragFx == order[static_cast<size_t>(k)]);
            y += h + px(6);
        }
        y += px(6);
        insertHeader.setBounds(0, y, w, px(22)); y += px(24);
        // insert cards in two columns when wide enough
        const int cols = w >= px(1100) ? 2 : 1;
        const int cw = (w - (cols - 1) * px(6)) / cols;
        std::array<int, 2> colY { y, y };
        for (auto* card : insertCards) {
            const int c = colY[0] <= colY[1] || cols == 1 ? 0 : 1;
            const int h = card->layoutHeight(cw);
            card->setBounds(c * (cw + px(6)), colY[static_cast<size_t>(c)], cw, h);
            colY[static_cast<size_t>(c)] += h + px(6);
        }
        content.setSize(w, juce::jmax(colY[0], colY[1]));
    }
    void dropIndicator() {
        if (dragFx < 0) return;
        for (auto* c : sendCards) c->setHighlighted(false);
        sendCards[dragFx]->setHighlighted(true);
    }
    void finishDrag() {
        if (dragFx < 0) return;
        const int f = dragFx;
        dragFx = -1;
        // drop position = number of other cards whose centre lies above the pointer
        int pos = 0;
        for (int k = 0; k < kNumFx; ++k)
            if (k != f && sendCards[k]->getBounds().getCentreY() < dragY) ++pos;
        moveFx(f, pos);
        layoutContent();
    }

    juce::Label title, targetLabel, sendHeader, insertHeader;
    juce::ComboBox targetBox;
    LevelMeter targetMeter;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<SendCard> sendCards;
    juce::OwnedArray<InsertCard> insertCards;
    FxTarget target;
    bool live = false;
    int lastInputs = -1, dragFx = -1, dragY = 0;
};
}
std::unique_ptr<Page> makeFxPage(EditorContext& c) { return std::make_unique<FxPage>(c); }
}
