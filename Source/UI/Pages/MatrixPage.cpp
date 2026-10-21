// Matrix page (GUI_SPEC section 7, ROUTING_SPEC): two coordinated MatrixGrids plus a destination headroom row:
//   (a) inputs & groups -> groups & FX sends (RoutingState assign, input/group FX-send parameters with pre/post)
//   (b) processed inputs (direct) and bus sources (groups, FX returns, playback, master) -> output channels,
//       with a per-output headroom estimate footer and feedback-risk badges on direct mic -> output cells.
// Illegal routes are hatched with the RouteValidator reason, hovering highlights the complete signal path, the
// validator's warnings are listed, and wiring templates can be applied (with confirmation; undoable).
#include "UI/Pages.h"
#include "DSP/RouteRisk.h"
#include "Routing/RouteValidator.h"
#include "Routing/WiringTemplates.h"
#include "UI/EditKit.h"
#include "UI/MatrixGrid.h"
#include "UI/RoutingUndo.h"
#include "UI/Ticker.h"
#include "UI/Widgets.h"

namespace crowdmike::ui {
using namespace crowdmike::params;
namespace RV = RouteValidator;

namespace {
const char* const kFxNames[kNumFx] = { "Delay", "Echo", "Tape", "Reverse", "Noise", "Reverb", "Multiplier" };
// Display order of the bus-source rows of grid B -> persisted bus row (Limits.h). The FX returns appended to the persisted layout
// (rows 30..33) are shown with the other FX returns; the stored row indices are unaffected.
int displayBusRow(int d) {
    constexpr int extra = kBusSrcCount - kBusSrcFxExtraBase;
    if (d < kBusSrcPlayback) return d;
    if (d < kBusSrcPlayback + extra) return kBusSrcFxExtraBase + (d - kBusSrcPlayback);
    return kBusSrcPlayback + (d - kBusSrcPlayback - extra);
}
constexpr float kNominalSourceDb = -18.0f;    // assumed nominal source peak level for the headroom estimate

const char* className(NodeClass c) {
    switch (c) {
        case NodeClass::RawInput: return "raw input";
        case NodeClass::ProcInput: return "processed input";
        case NodeClass::Group: return "group";
        case NodeClass::FxSend: return "FX send";
        case NodeClass::FxReturn: return "FX return";
        case NodeClass::Playback: return "playback";
        case NodeClass::Master: return "master bus";
        case NodeClass::Recorder: return "recorder";
        case NodeClass::Output: return "output";
    }
    return "?";
}
float dbToLin(float db) { return db <= -79.9f ? 0.0f : juce::Decibels::decibelsToGain(db); }
float linToDb(float g) { return g <= 0.0f ? -80.0f : juce::jlimit(-80.0f, 6.0f, juce::Decibels::gainToDecibels(g)); }

class MatrixPage final : public Page, private Tickable {
public:
    explicit MatrixPage(EditorContext& c) : Page(c) {
        kit::styleLabel(title, 16.0f, true, palette().accent);
        title.setText("Routing matrix", juce::dontSendNotification);
        addAndMakeVisible(title);
        for (int t = 0; t < static_cast<int>(WiringTemplates::Id::Count); ++t)
            templateBox.addItem(WiringTemplates::name(static_cast<WiringTemplates::Id>(t)), t + 1);
        templateBox.setSelectedId(1, juce::dontSendNotification);
        templateBox.setTitle("Wiring template");
        templateBox.setTooltip("Wiring templates (ROUTING_SPEC section 8)");
        templateBox.onChange = [this] { updateTemplateText(); };
        addAndMakeVisible(templateBox);
        applyBtn.setTooltip("Apply the selected wiring template (asks for confirmation: it overwrites the routing; undoable)");
        applyBtn.setComponentID("matrix-apply-template");
        applyBtn.onClick = [this] { confirmTemplate(); };
        addAndMakeVisible(applyBtn);
        kit::styleLabel(templateText, 11.5f, false, palette().textMuted);
        addAndMakeVisible(templateText);
        undoBtn.setTooltip("Undo the last parameter / routing change (Ctrl+Z)");
        redoBtn.setTooltip("Redo (Ctrl+Shift+Z)");
        undoBtn.onClick = [this] { proc.getUndoManager().undo(); refreshAll(); };
        redoBtn.onClick = [this] { proc.getUndoManager().redo(); refreshAll(); };
        addAndMakeVisible(undoBtn);
        addAndMakeVisible(redoBtn);

        for (auto* l : { &labelA, &labelB }) { kit::styleLabel(*l, 13.0f, true, palette().accent); content.addAndMakeVisible(*l); }
        labelA.setText("A  INPUTS & GROUPS -> GROUPS & FX SENDS", juce::dontSendNotification);
        labelB.setText("B  SOURCES -> OUTPUT CHANNELS   (footer: headroom estimate per output)", juce::dontSendNotification);
        setupGridA();
        setupGridB();
        content.addAndMakeVisible(gridA);
        content.addAndMakeVisible(gridB);
        viewport.setViewedComponent(&content, false);
        addAndMakeVisible(viewport);

        kit::styleLabel(pathLabel, 12.0f, false, palette().text, juce::Justification::topLeft);
        pathLabel.setText("Hover a cell or header to highlight its complete signal path.", juce::dontSendNotification);
        addAndMakeVisible(pathLabel);
        kit::styleLabel(warnTitle, 13.0f, true, palette().accent);
        warnTitle.setText("WARNINGS", juce::dontSendNotification);
        addAndMakeVisible(warnTitle);
        warnings.setMultiLine(true);
        warnings.setReadOnly(true);
        warnings.setScrollbarsShown(true);
        warnings.setCaretVisible(false);
        warnings.setFont(font(12.0f));
        warnings.setTitle("Routing warnings");
        warnings.setComponentID("matrix-warnings");
        addAndMakeVisible(warnings);
        kit::styleLabel(legend, 11.0f, false, palette().textMuted, juce::Justification::topLeft);
        legend.setText(juce::String::fromUTF8("\xc2\xb7 off   \xe2\x97\x8f unity   -6 level (dB)   PRE pre-fader   \xe2\x9c\x95 hatched = illegal route   "
                                              "\xe2\x96\xb2 ! / !! feedback risk (advisory)\nClick toggle | drag level | dbl-click value | Ctrl-click unity | "
                                              "arrows + Space + / - on the keyboard"), juce::dontSendNotification);
        addAndMakeVisible(legend);
        updateTemplateText();
        refreshAll();
    }

    void pageShown() override { live = true; refreshAll(); }
    void pageHidden() override { live = false; }
    void tick() override {
        if (!live) return;
        const auto rev = proc.getRouting().revision();
        const int nIn = proc.getActiveInputChannelCount(), nOut = proc.getActiveOutputChannelCount();
        if (++counter % 15 == 0 || rev != lastRev || nIn != lastIn || nOut != lastOut) refreshAll();
    }
    void resized() override {
        auto r = getLocalBounds().reduced(px(8));
        auto top = r.removeFromTop(px(28));
        title.setBounds(top.removeFromLeft(px(150)));
        redoBtn.setBounds(top.removeFromRight(px(60)).reduced(px(1)));
        undoBtn.setBounds(top.removeFromRight(px(60)).reduced(px(1)));
        top.removeFromRight(px(8));
        templateBox.setBounds(top.removeFromLeft(px(240)).reduced(0, px(1)));
        applyBtn.setBounds(top.removeFromLeft(px(130)).reduced(px(3), px(1)));
        templateText.setBounds(top);
        r.removeFromTop(px(6));
        auto side = r.removeFromRight(juce::jlimit(px(220), px(340), r.getWidth() / 4));
        r.removeFromRight(px(6));
        pathLabel.setBounds(side.removeFromTop(px(96)));
        legend.setBounds(side.removeFromBottom(px(96)));
        warnTitle.setBounds(side.removeFromTop(px(20)));
        warnings.setBounds(side);
        viewport.setBounds(r);
        layoutContent();
    }

    // For tests / keyboard shortcuts
    MatrixGrid& getGridA() noexcept { return gridA; }
    MatrixGrid& getGridB() noexcept { return gridB; }
    void applyTemplateNow(int t) {
        auto& routing = proc.getRouting();
        const auto before = routing.toValueTree();
        proc.applyWiringTemplate(t);    // starts its own undo transaction (parameters)
        const auto after = routing.toValueTree();
        proc.getUndoManager().perform(new RoutingSnapshotAction(routing, before, after), "Apply wiring template");
        ctx.say(juce::String("Wiring template applied: ") + WiringTemplates::name(static_cast<WiringTemplates::Id>(t)));
        refreshAll();
    }

private:
    int nIn() const { return juce::jlimit(1, kMaxInputs, proc.getActiveInputChannelCount()); }
    int nOut() const { return juce::jlimit(1, kMaxOutputs, proc.getActiveOutputChannelCount()); }

    // ---------------------------------------------------------------- grid A: inputs/groups -> groups/FX sends
    int rowNodeA(int r) const { return r < nIn() ? RV::nodeInput(r) : RV::nodeGroup(r - nIn()); }
    NodeClass rowClassA(int r) const { return r < nIn() ? NodeClass::ProcInput : NodeClass::Group; }
    int colNodeA(int c) const { return c < kMaxGroups ? RV::nodeGroup(c) : RV::nodeFxSend(c - kMaxGroups); }
    NodeClass colClassA(int c) const { return c < kMaxGroups ? NodeClass::Group : NodeClass::FxSend; }
    int sendParam(int r, int f) const {
        return r < nIn() ? ParamBank::index(Mod::Input, r, inputSendParam(f)) : ParamBank::index(Mod::Group, r - nIn(), groupSendParam(f));
    }
    void setupGridA() {
        gridA.setComponentID("matrix-grid-a");
        gridA.setTitle("Inputs and groups to groups and FX sends");
        gridA.setCellSize(62, 26);
        gridA.setHeaderSizes(170, 44);
        gridA.numRows = [this] { return nIn() + kMaxGroups; };
        gridA.numCols = [] { return kMaxGroups + kNumFx; };
        gridA.rowName = [this](int r) { return r < nIn() ? juce::String(r + 1) + "  " + proc.getShow().inputDisplayName(r)
                                                         : "G" + juce::String(r - nIn() + 1) + "  " + proc.getShow().groupDisplayName(r - nIn()); };
        gridA.colName = [this](int c) { return c < kMaxGroups ? proc.getShow().groupDisplayName(c) : juce::String(kFxNames[c - kMaxGroups]) + " send"; };
        gridA.rowSection = [this](int r) { return r < nIn() ? juce::String("Processed inputs") : juce::String("Groups"); };
        gridA.getGain = [this](int r, int c) -> float {
            if (c < kMaxGroups) return r < nIn() ? proc.getRouting().getAssign(r, c) : 0.0f;
            return dbToLin(kit::actual(proc, sendParam(r, c - kMaxGroups)));
        };
        gridA.setLive = [this](int r, int c, float g) {
            if (c < kMaxGroups) proc.getRouting().setAssign(r, c, g);
            else kit::setActual(proc, sendParam(r, c - kMaxGroups), linToDb(g));
        };
        gridA.commit = [this](int r, int c, float o, float n) {
            if (c < kMaxGroups) performRouteEdit(proc.getUndoManager(), proc.getRouting(), RouteMatrix::Assign, r, c, o, n, "Group assignment");
            else {
                proc.getUndoManager().beginNewTransaction("FX send level");
                kit::setActual(proc, sendParam(r, c - kMaxGroups), linToDb(n));
            }
            refreshAll();
        };
        gridA.blockReason = [this](int r, int c) { return blockReason(rowNodeA(r), rowClassA(r), colNodeA(c), colClassA(c)); };
        gridA.isPre = [this](int r, int c) { return c >= kMaxGroups && r < nIn() && kit::isOn(proc, ParamBank::index(Mod::Input, r, InputP::FxPre)); };
        gridA.togglePre = [this](int r, int c) {
            if (c < kMaxGroups) return;
            if (r >= nIn()) { ctx.say("Group FX sends are always post-fader"); return; }
            const int idx = ParamBank::index(Mod::Input, r, InputP::FxPre);
            proc.getUndoManager().beginNewTransaction("FX sends pre/post");
            kit::setActual(proc, idx, kit::isOn(proc, idx) ? 0.0f : 1.0f);
        };
        gridA.cellInfo = [this](int r, int c) -> juce::String {
            if (c >= kMaxGroups && r < nIn()) return "Pre/post applies to all FX sends of this input (right-click or P to change).";
            return {};
        };
        gridA.onHover = [this](int r, int c) { hover(r >= 0 ? rowNodeA(r) : -1, c >= 0 ? colNodeA(c) : -1); };
        gridA.onRowClick = [this](int r) { ctx.select(r < nIn() ? Selection { Selection::Kind::Input, r } : Selection { Selection::Kind::Group, r - nIn() }); };
        gridA.cellHighlighted = [this](int r, int c) { return onPath(rowNodeA(r)) && onPath(colNodeA(c)) && gridA.getGain(r, c) != 0.0f; };
        gridA.rowHighlighted = [this](int r) { return onPath(rowNodeA(r)); };
        gridA.colHighlighted = [this](int c) { return onPath(colNodeA(c)); };
    }

    // ---------------------------------------------------------------- grid B: sources -> outputs
    int busRow(int r) const { return displayBusRow(r - nIn()); }
    int rowNodeB(int r) const {
        if (r < nIn()) return RV::nodeInput(r);
        const int b = busRow(r);
        if (b < kBusSrcFxBase) return RV::nodeGroup(b / 2);
        if (busRowIsFxReturn(b)) return RV::nodeFxReturn(busRowFxIndex(b));
        if (b < kBusSrcMaster) return RV::nodePlayback();
        return RV::nodeMaster();
    }
    NodeClass rowClassB(int r) const {
        if (r < nIn()) return NodeClass::ProcInput;
        const int b = busRow(r);
        if (b < kBusSrcFxBase) return NodeClass::Group;
        if (busRowIsFxReturn(b)) return NodeClass::FxReturn;
        if (b < kBusSrcMaster) return NodeClass::Playback;
        return NodeClass::Master;
    }
    juce::String rowNameB(int r) const {
        auto& show = proc.getShow();
        if (r < nIn()) return juce::String(r + 1) + "  " + show.inputDisplayName(r) + " (direct)";
        const int b = busRow(r);
        const juce::String lr = (b % 2) == 0 ? " L" : " R";
        if (b < kBusSrcFxBase) return show.groupDisplayName(b / 2) + lr;
        if (busRowIsFxReturn(b)) return juce::String(kFxNames[busRowFxIndex(b)]) + " return" + lr;
        if (b < kBusSrcMaster) return "Playback" + lr;
        return "Master" + lr;
    }
    float getB(int r, int o) const { return r < nIn() ? proc.getRouting().getDirect(r, o) : proc.getRouting().getBus(busRow(r), o); }
    // Linear gain of the source feeding row r (trim / return / level) for the headroom estimate.
    float sourceGain(int r) const {
        auto& p = const_cast<CrowdMikeAudioProcessor&>(proc);
        if (r < nIn()) return dbToLin(kit::actual(p, ParamBank::index(Mod::Input, r, InputP::Trim)));
        const int b = busRow(r);
        if (b < kBusSrcFxBase) return dbToLin(kit::actual(p, ParamBank::index(Mod::Group, b / 2, GroupP::Trim)));
        if (busRowIsFxReturn(b)) { const int f = busRowFxIndex(b);
            static const Mod mods[kNumFx] = { Mod::FxDelay, Mod::FxEcho, Mod::FxTape, Mod::FxReverse, Mod::FxNoise, Mod::FxReverb, Mod::FxMulti };
            static const int rets[kNumFx] = { DelayP::Return, EchoP::Return, TapeP::Return, ReverseP::Return, NoiseP::Return, ReverbP::Return, MultiP::Return };
            return dbToLin(kit::actual(p, ParamBank::index(mods[f], 0, rets[f]))); }
        if (b < kBusSrcMaster) return dbToLin(kit::actual(p, ParamBank::index(Mod::Global, 0, GlobalP::PlaybackLevel)));
        return dbToLin(kit::actual(p, ParamBank::index(Mod::Master, 0, MasterP::Level)));
    }
    dsp::RiskResult riskFor(int input, int o) const { return dsp::scoreRisk(riskInputs(input, o)); }
    dsp::RiskInputs riskInputs(int input, int o) const {
        auto& p = const_cast<CrowdMikeAudioProcessor&>(proc);
        dsp::RiskInputs in;
        const float g = proc.getRouting().getDirect(input, o);
        in.routeExists = std::abs(g) > 1.0e-4f;
        in.inputTrimDb = kit::actual(p, ParamBank::index(Mod::Input, input, InputP::Trim));
        in.busGainDb = juce::Decibels::gainToDecibels(std::abs(g), -100.0f);
        in.outputGainDb = kit::actual(p, ParamBank::index(Mod::Output, o, OutputP::Trim));
        if (kit::isOn(p, ParamBank::index(Mod::Input, input, InputP::InCompOn)))
            in.compressorMakeupDb = kit::actual(p, ParamBank::index(Mod::Input, input, InputP::InCompMakeup));
        in.persistentPeakSeverity = p.getEngine().getFeedbackTap() == 1 + input ? topSeverity : 0.0f;
        const auto& mz = proc.getShow().input(input).zone;
        in.sameZone = mz.isNotEmpty() && mz.equalsIgnoreCase(proc.getShow().output(o).zone);
        return in;
    }
    MatrixGrid::Footer headroom(int o) const {
        auto& p = const_cast<CrowdMikeAudioProcessor&>(proc);
        float sum = 0.0f;
        int sources = 0;
        for (int r = 0; r < nIn() + kBusSrcCount; ++r) {
            const float g = std::abs(getB(r, o));
            if (g < 1.0e-4f) continue;
            sum += g * sourceGain(r);
            ++sources;
        }
        MatrixGrid::Footer f;
        if (sources == 0 || sum <= 0.0f) { f.text = "unused"; f.tip = proc.getShow().outputDisplayName(o) + ": no sources routed"; return f; }
        const float sumDb = juce::Decibels::gainToDecibels(sum) + kit::actual(p, ParamBank::index(Mod::Output, o, OutputP::Trim));
        const float ceiling = kit::actual(p, ParamBank::index(Mod::Global, 0, GlobalP::OutCeiling));
        const float hr = ceiling - (kNominalSourceDb + sumDb);
        f.text = "~" + juce::String(hr, 1) + " dB";
        f.level = hr < 0.0f ? 2 : (hr < 6.0f ? 1 : 0);
        f.tip = proc.getShow().outputDisplayName(o) + ": " + juce::String(sources) + " source(s), summed gain " + kit::fmtDb(sumDb)
                + " dB incl. output trim.\nHeadroom ~ " + juce::String(hr, 1) + " dB below the " + juce::String(ceiling, 1)
                + " dBFS limiter ceiling, assuming every source peaks at " + juce::String(kNominalSourceDb, 0)
                + " dBFS and sums coherently (worst case). Advisory only.";
        return f;
    }
    void setupGridB() {
        gridB.setComponentID("matrix-grid-b");
        gridB.setTitle("Sources to output channels");
        gridB.setCellSize(52, 24);
        gridB.setHeaderSizes(170, 34);
        gridB.numRows = [this] { return nIn() + kBusSrcCount; };
        gridB.numCols = [] { return kMaxOutputs; };
        gridB.rowName = [this](int r) { return rowNameB(r); };
        gridB.colName = [this](int o) { return proc.getShow().outputDisplayName(o); };
        gridB.colTip = [this](int o) { return proc.getShow().outputDisplayName(o) + (o < nOut() ? juce::String() : juce::String(" (inactive: not in the current device/host layout)")); };
        gridB.colActive = [this](int o) { return o < nOut(); };
        gridB.rowSection = [this](int r) -> juce::String {
            if (r < nIn()) return "Direct: processed inputs (live mics)";
            const int b = busRow(r);
            if (b < kBusSrcFxBase) return "Groups";
            if (busRowIsFxReturn(b)) return "FX returns";
            if (b < kBusSrcMaster) return "Playback";
            return "Master crowd bus";
        };
        gridB.getGain = [this](int r, int o) { return getB(r, o); };
        gridB.setLive = [this](int r, int o, float g) {
            if (r < nIn()) proc.getRouting().setDirect(r, o, g); else proc.getRouting().setBus(busRow(r), o, g);
        };
        gridB.commit = [this](int r, int o, float old, float n) {
            if (r < nIn()) performRouteEdit(proc.getUndoManager(), proc.getRouting(), RouteMatrix::Direct, r, o, old, n, "Direct route");
            else performRouteEdit(proc.getUndoManager(), proc.getRouting(), RouteMatrix::Bus, busRow(r), o, old, n, "Output route");
            refreshAll();
        };
        gridB.blockReason = [this](int r, int o) { return blockReason(rowNodeB(r), rowClassB(r), RV::nodeOutput(o), NodeClass::Output); };
        gridB.riskLevel = [this](int r, int o) { return r < nIn() ? riskFor(r, o).level : 0; };
        gridB.cellInfo = [this](int r, int o) -> juce::String {
            if (r >= nIn()) return {};
            const auto risk = riskFor(r, o);
            if (proc.getRouting().getDirect(r, o) == 0.0f) return "Direct live-microphone route: enabling it adds a feedback risk check.";
            return "Feedback risk (advisory): " + juce::String(risk.level >= 2 ? "HIGH" : (risk.level == 1 ? "caution" : "low")) + ", score "
                   + juce::String(risk.score, 2) + " (" + risk.reason + "). Zones: mic '" + proc.getShow().input(r).zone + "', speaker '"
                   + proc.getShow().output(o).zone + "'.";
        };
        gridB.footer = [this](int o) { return headroom(o); };
        gridB.footerName = "Headroom estimate";
        gridB.onHover = [this](int r, int o) { hover(r >= 0 ? rowNodeB(r) : -1, o >= 0 ? RV::nodeOutput(o) : -1); };
        gridB.onRowClick = [this](int r) {
            if (r < nIn()) ctx.select({ Selection::Kind::Input, r });
            else if (busRow(r) < kBusSrcFxBase) ctx.select({ Selection::Kind::Group, busRow(r) / 2 });
            else if (busRow(r) >= kBusSrcMaster && busRow(r) < kBusSrcLegacyCount) ctx.select({ Selection::Kind::Master, 0 });
        };
        gridB.cellHighlighted = [this](int r, int o) { return onPath(rowNodeB(r)) && onPath(RV::nodeOutput(o)) && getB(r, o) != 0.0f; };
        gridB.rowHighlighted = [this](int r) { return onPath(rowNodeB(r)); };
        gridB.colHighlighted = [this](int o) { return onPath(RV::nodeOutput(o)); };
    }

    // ---------------------------------------------------------------- legality, paths, warnings
    juce::String blockReason(int fromNode, NodeClass from, int toNode, NodeClass to) const {
        if (!RV::isLegalRoute(from, to))
            return juce::String("a ") + className(from) + " cannot feed a " + className(to)
                   + (from == to ? " (same stage: it would form a loop)" : " in the feed-forward topology (Input -> Group -> FX -> Master -> Output)");
        if (to != NodeClass::Output && RV::wouldCreateCycle(RV::nodeCount(), edges, { fromNode, toNode })) {
            bool exists = false;
            for (const auto& e : edges) exists = exists || (e.from == fromNode && e.to == toNode);
            if (!exists) return "this route would create a signal cycle";
        }
        return {};
    }
    bool onPath(int node) const { return node >= 0 && node < static_cast<int>(pathNodes.size()) && pathNodes[static_cast<size_t>(node)]; }
    juce::String nodeName(int n) const {
        auto& show = proc.getShow();
        if (n < RV::nodeGroup(0)) return show.inputDisplayName(n);
        if (n < RV::nodeFxSend(0)) return show.groupDisplayName(n - RV::nodeGroup(0));
        if (n < RV::nodeFxReturn(0)) return juce::String(kFxNames[n - RV::nodeFxSend(0)]) + " send";
        if (n < RV::nodePlayback()) return juce::String(kFxNames[n - RV::nodeFxReturn(0)]) + " return";
        if (n == RV::nodePlayback()) return "Playback";
        if (n == RV::nodeMaster()) return "Master";
        return show.outputDisplayName(n - RV::nodeOutput(0));
    }
    void hover(int fromNode, int toNode) {
        pathNodes.assign(static_cast<size_t>(RV::nodeCount()), false);
        if (fromNode < 0 && toNode < 0) {
            pathLabel.setText("Hover a cell or header to highlight its complete signal path.", juce::dontSendNotification);
        } else {
            const auto mark = [this](const std::vector<int>& v) { for (int n : v) pathNodes[static_cast<size_t>(n)] = true; };
            if (fromNode >= 0) mark(RV::reachingTo(RV::nodeCount(), edges, fromNode));
            if (toNode >= 0) mark(RV::reachableFrom(RV::nodeCount(), edges, toNode));
            if (fromNode >= 0 && toNode < 0) mark(RV::reachableFrom(RV::nodeCount(), edges, fromNode));
            if (toNode >= 0 && fromNode < 0) mark(RV::reachingTo(RV::nodeCount(), edges, toNode));
            juce::StringArray names;
            for (int n = 0; n < RV::nodeCount(); ++n)
                if (pathNodes[static_cast<size_t>(n)] && !(n < RV::nodeGroup(0) && n >= nIn())) names.add(nodeName(n));
            pathLabel.setText("PATH: " + names.joinIntoString(juce::String::fromUTF8(" \xe2\x86\x92 ")), juce::dontSendNotification);
        }
        gridA.repaint();
        gridB.repaint();
    }
    void refreshAll() {
        auto& routing = proc.getRouting();
        lastRev = routing.revision();
        lastIn = proc.getActiveInputChannelCount();
        lastOut = proc.getActiveOutputChannelCount();
        edges = RV::edgesFromState(routing);
        for (int i = 0; i < nIn(); ++i)
            for (int f = 0; f < kNumFx; ++f)
                if (kit::actual(proc, ParamBank::index(Mod::Input, i, inputSendParam(f))) > -79.9f) edges.push_back({ RV::nodeInput(i), RV::nodeFxSend(f) });
        dsp::FeedbackCandidate c[1];
        topSeverity = proc.getEngine().feedback().getCandidates(c, 1) > 0 ? c[0].severity : 0.0f;
        if (pathNodes.size() != static_cast<size_t>(RV::nodeCount())) pathNodes.assign(static_cast<size_t>(RV::nodeCount()), false);

        juce::String text;
        int count = 0;
        for (const auto& issue : RV::validate(routing, nIn(), nOut())) {
            const char* glyph = issue.severity == RouteIssue::Error ? "\xe2\x9c\x95 ERROR: " : (issue.severity == RouteIssue::Warning ? "\xe2\x96\xb2 WARNING: " : "\xe2\x93\x98 ");
            text << juce::String::fromUTF8(glyph) << issue.message << "\n";
            ++count;
        }
        for (int i = 0; i < nIn(); ++i)
            for (int o = 0; o < nOut(); ++o) {
                const auto risk = riskFor(i, o);
                if (risk.level == 0) continue;
                text << juce::String::fromUTF8(risk.level >= 2 ? "\xe2\x9c\x95 HIGH RISK: " : "\xe2\x96\xb2 CAUTION: ") << proc.getShow().inputDisplayName(i)
                     << juce::String::fromUTF8(" \xe2\x86\x92 ") << proc.getShow().outputDisplayName(o) << ": feedback risk " << juce::String(risk.score, 2)
                     << " (" << risk.reason << ", loop gain " << kit::fmtDb(dsp::routeLoopGainDb(riskInputs(i, o))) << " dB" << ")\n";
                ++count;
            }
        for (int o = 0; o < nOut(); ++o) {
            const auto f = headroom(o);
            if (f.level >= 1) { text << juce::String::fromUTF8(f.level >= 2 ? "\xe2\x9c\x95 " : "\xe2\x96\xb2 ") << proc.getShow().outputDisplayName(o) << ": headroom " << f.text << " (estimate)\n"; ++count; }
        }
        if (count == 0) text = juce::String::fromUTF8("\xe2\x97\x8f No routing warnings.");
        if (text != warnings.getText()) warnings.setText(text, false);
        warnTitle.setText("WARNINGS (" + juce::String(count) + ")", juce::dontSendNotification);
        undoBtn.setEnabled(proc.getUndoManager().canUndo());
        redoBtn.setEnabled(proc.getUndoManager().canRedo());
        if (gridA.getIdealHeight() != gridA.getHeight() || gridB.getIdealHeight() != gridB.getHeight()) layoutContent();
        gridA.repaint();
        gridB.repaint();
    }
    void layoutContent() {
        int y = 0;
        const int w = juce::jmax(gridA.getIdealWidth(), gridB.getIdealWidth(), viewport.getWidth() - viewport.getScrollBarThickness());
        labelA.setBounds(0, y, w, px(22)); y += px(24);
        gridA.setBounds(0, y, gridA.getIdealWidth(), gridA.getIdealHeight()); y += gridA.getHeight() + px(14);
        labelB.setBounds(0, y, w, px(22)); y += px(24);
        gridB.setBounds(0, y, gridB.getIdealWidth(), gridB.getIdealHeight()); y += gridB.getHeight() + px(8);
        content.setSize(w, y);
    }
    void updateTemplateText() {
        const auto id = static_cast<WiringTemplates::Id>(juce::jmax(0, templateBox.getSelectedId() - 1));
        templateText.setText(WiringTemplates::description(id), juce::dontSendNotification);
        templateText.setTooltip(WiringTemplates::description(id));
    }
    void confirmTemplate() {
        const int t = juce::jmax(0, templateBox.getSelectedId() - 1);
        const auto opts = juce::MessageBoxOptions()
                              .withIconType(juce::MessageBoxIconType::WarningIcon)
                              .withTitle("Apply wiring template?")
                              .withMessage(juce::String("\"") + WiringTemplates::name(static_cast<WiringTemplates::Id>(t))
                                           + "\" overwrites the current routing (and a few related parameters). You can undo it.")
                              .withButton("Apply")
                              .withButton("Cancel")
                              .withAssociatedComponent(this);
        juce::Component::SafePointer<MatrixPage> safe(this);
        juce::AlertWindow::showAsync(opts, [safe, t](int result) { if (safe != nullptr && result == 1) safe->applyTemplateNow(t); });
    }

    juce::Label title, templateText, labelA, labelB, pathLabel, warnTitle, legend;
    juce::ComboBox templateBox;
    juce::TextButton applyBtn { "Apply template..." }, undoBtn { "Undo" }, redoBtn { "Redo" };
    juce::Viewport viewport;
    juce::Component content;
    MatrixGrid gridA, gridB;
    juce::TextEditor warnings;
    std::vector<Edge> edges;
    std::vector<bool> pathNodes;
    float topSeverity = 0.0f;
    bool live = false;
    std::uint32_t lastRev = 0xffffffffu;
    int lastIn = -1, lastOut = -1, counter = 0;
};
}
std::unique_ptr<Page> makeMatrixPage(EditorContext& c) { return std::make_unique<MatrixPage>(c); }
}
