// Groups page (GUI_SPEC section 5): eight crowd-group strips with name, input membership, trim, balance, mute/solo,
// polarity, reinforcement delay, HPF/LPF, compressor (with presets), limiter, FX sends, width and a routing
// destination summary (from the output bus matrix) with a jump to the Matrix page.
#include "UI/Pages.h"
#include "UI/EditKit.h"
#include "UI/MatrixGrid.h"
#include "UI/Meters.h"
#include "UI/RoutingUndo.h"
#include "UI/Widgets.h"

namespace crowdmike::ui {
using namespace crowdmike::params;

namespace {
int gidx(int g, int e) { return ParamBank::index(Mod::Group, g, e); }
std::unique_ptr<juce::Component> slider(CrowdMikeAudioProcessor& p, int idx, int textW = 52) {
    return std::make_unique<ParamSlider>(p, idx, juce::Slider::LinearHorizontal, textW);
}

// Outputs fed by a group through the bus matrix, e.g. "Out 1, Out 2".
juce::String groupDestinations(CrowdMikeAudioProcessor& proc, int g) {
    juce::StringArray outs;
    auto& r = proc.getRouting();
    for (int o = 0; o < kMaxOutputs; ++o)
        if (std::abs(r.getBus(RoutingState::busRowForGroup(g, 0), o)) > 1.0e-4f || std::abs(r.getBus(RoutingState::busRowForGroup(g, 1), o)) > 1.0e-4f)
            outs.add(proc.getShow().outputDisplayName(o));
    return juce::String::fromUTF8("\xe2\x86\x92 Master") + (outs.isEmpty() ? juce::String() : ", " + outs.joinIntoString(", "));
}

class GroupRow final : public kit::Card {
public:
    GroupRow(EditorContext& c, int group) : ctx(c), proc(c.proc), g(group),
        mute(proc, gidx(g, GroupP::Mute), "M"), solo(proc, gidx(g, GroupP::Solo), "S"),
        pol(proc, gidx(g, GroupP::Pol), juce::String::fromUTF8("\xc3\x98")),
        hpOn(proc, gidx(g, GroupP::HpOn), "HPF"), lpOn(proc, gidx(g, GroupP::LpOn), "LPF"),
        compOn(proc, gidx(g, GroupP::GrpCompOn), "COMP"), limOn(proc, gidx(g, GroupP::GrpLimOn), "LIM"),
        trim("Trim", slider(proc, gidx(g, GroupP::Trim), 56), 30), balance("Bal", slider(proc, gidx(g, GroupP::Pan), 46), 24),
        delay("Delay", slider(proc, gidx(g, GroupP::DelayMs), 66), 34),
        hpHz("Hz", slider(proc, gidx(g, GroupP::HpHz)), 18), lpHz("Hz", slider(proc, gidx(g, GroupP::LpHz)), 18),
        thr("Thr", slider(proc, gidx(g, GroupP::GrpCompThr), 50), 24), ratio("Ratio", slider(proc, gidx(g, GroupP::GrpCompRatio), 44), 32),
        ceil("Ceil", slider(proc, gidx(g, GroupP::GrpLimCeil), 50), 26), width("Width", slider(proc, gidx(g, GroupP::Width), 44), 36),
        grMeter(18.0f) {
        setComponentID("group-row-" + juce::String(g));
        setTitle("Group " + juce::String(g + 1));
        static const char* sendNames[kNumFx] = { "Dly", "Echo", "Tape", "Rev", "Noise", "Verb", "Multi" };
        for (int f = 0; f < kNumFx; ++f) {
            auto* s = sends.add(new kit::Captioned(sendNames[f], slider(proc, gidx(g, groupSendParam(f)), 46), 30));
            s->setTooltip(juce::String("Post-fader send to the ") + sendNames[f] + " effect (dB, -80 = off)");
        }
        meterL.source = [this] { return live ? proc.getMeters().groupPeakL[static_cast<size_t>(g)].getAndReset() : 0.0f; };
        meterR.source = [this] { return live ? proc.getMeters().groupPeakR[static_cast<size_t>(g)].getAndReset() : 0.0f; };
        meterL.setTooltip("Group level L (click to clear clip)"); meterR.setTooltip("Group level R (click to clear clip)");
        grMeter.source = [this] { return live ? std::abs(proc.getMeters().groupGrDb[static_cast<size_t>(g)].load()) : 0.0f; };
        grMeter.setTitle("Group compressor gain reduction");

        kit::styleLabel(nameLabel, 13.0f, true);
        nameLabel.setEditable(false, true, false);
        nameLabel.setColour(juce::Label::backgroundColourId, palette().panelRaised);
        nameLabel.setTooltip("Group name: double-click to rename (stable ID " + ShowModel::groupId(g) + ")");
        nameLabel.setTitle("Group name");
        nameLabel.onTextChange = [this] { proc.getShow().group(g).name = nameLabel.getText().trim(); ctx.showModelChanged(); };
        kit::styleLabel(memberSummary, 11.5f, false, palette().textMuted);
        kit::styleLabel(routeSummary, 11.5f, false, palette().text);
        memberSummary.setInterceptsMouseClicks(false, false);
        routeSummary.setInterceptsMouseClicks(false, false);
        routeSummary.setTooltip("Routing destinations from the output bus matrix (groups always feed the master bus)");

        members.setComponentID("group-members-" + juce::String(g));
        members.setTitle("Input membership of group " + juce::String(g + 1));
        members.setShowHeaders(false, false);
        members.setCellSize(24, 26);
        members.numRows = [] { return 1; };
        members.numCols = [this] { return juce::jlimit(1, kMaxInputs, proc.getActiveInputChannelCount()); };
        members.rowName = [this](int) { return proc.getShow().groupDisplayName(g); };
        members.colName = [this](int i) { return proc.getShow().inputDisplayName(i); };
        members.cellLabel = [](int, int i) { return juce::String(i + 1); };
        members.getGain = [this](int, int i) { return proc.getRouting().getAssign(i, g); };
        members.setLive = [this](int, int i, float v) { proc.getRouting().setAssign(i, g, v); };
        members.commit = [this](int, int i, float o, float n) { performRouteEdit(proc.getUndoManager(), proc.getRouting(), RouteMatrix::Assign, i, g, o, n, "Group membership"); };

        mute.setTooltip("Mute group"); solo.setTooltip("Solo group");
        pol.setTooltip(juce::String::fromUTF8("Polarity \xc3\x98 (exact inversion; not crowd cancellation)"));
        delay.setTooltip("Reinforcement delay: intentional latency of this group (separate from alignment and creative delay)");
        compOn.setTooltip("Group compressor on/off"); limOn.setTooltip("Group limiter on/off");
        hpOn.setTooltip("High-pass on/off"); lpOn.setTooltip("Low-pass on/off");
        presetBtn.setTooltip("Compressor presets for this group");
        presetBtn.onClick = [this] {
            kit::PresetTarget t;
            t.module = Mod::Group; t.instance = g; t.factoryTarget = "group"; t.blockPrefixes = { "comp" };
            t.folderName = "group-comp"; t.title = "Group compressor presets";
            t.extraSections = { { "Filter presets", { "hp", "lp" } } };
            kit::showPresetMenu(proc, &presetBtn, t);
        };
        matrixBtn.setTooltip("Open the Matrix page to edit this group's output routing");
        matrixBtn.onClick = [this] { ctx.select({ Selection::Kind::Group, g }); if (ctx.navigate) ctx.navigate(PageId::Matrix); };

        for (juce::Component* comp : std::initializer_list<juce::Component*> { &meterL, &meterR, &nameLabel, &memberSummary, &members, &trim, &balance, &mute, &solo, &pol,
                                                                             &delay, &hpOn, &hpHz, &lpOn, &lpHz, &compOn, &thr, &ratio, &grMeter, &presetBtn, &limOn, &ceil,
                                                                             &width, &routeSummary, &matrixBtn })
            addAndMakeVisible(comp);
        for (auto* s : sends) addAndMakeVisible(s);
        refresh();
    }
    void setLive(bool l) { live = l; }
    void setSelected(bool s) { setHighlighted(s); }
    void refresh() {
        if (!nameLabel.isBeingEdited())
            nameLabel.setText(juce::String(g + 1) + "  " + proc.getShow().groupDisplayName(g), juce::dontSendNotification);
        juce::StringArray names;
        const int n = proc.getActiveInputChannelCount();
        for (int i = 0; i < n; ++i) if (proc.getRouting().getAssign(i, g) > 1.0e-4f) names.add(proc.getShow().inputDisplayName(i));
        memberSummary.setText(names.isEmpty() ? juce::String("no inputs") : juce::String(names.size()) + " in: " + names.joinIntoString(", "), juce::dontSendNotification);
        routeSummary.setText(groupDestinations(proc, g), juce::dontSendNotification);
        members.repaint();
    }
    int layoutHeight(int w) { return flowItems(w, false) + px(10); }
    void resized() override { flowItems(getWidth(), true); }
    void mouseDown(const juce::MouseEvent&) override { ctx.select({ Selection::Kind::Group, g }); }

    MatrixGrid members;
    juce::TextButton matrixBtn { juce::String::fromUTF8("Matrix \xe2\x96\xb8") };

private:
    int flowItems(int w, bool apply) {
        auto r = juce::Rectangle<int>(0, 0, w, 10000).reduced(px(5));
        if (apply) {
            meterL.setBounds(r.getX(), r.getY(), px(6), getHeight() - px(10));
            meterR.setBounds(r.getX() + px(7), r.getY(), px(6), getHeight() - px(10));
        }
        r.removeFromLeft(px(18));
        const int memberW = juce::jlimit(1, kMaxInputs, proc.getActiveInputChannelCount()) * 24 + 1;
        std::vector<kit::FlowItem> items {
            { &nameLabel, 150 }, { &members, memberW }, { &memberSummary, 170 }, { &trim, 170 }, { &balance, 130 }, { &mute, 32 }, { &solo, 32 }, { &pol, 34 },
            { &delay, 170 }, { &hpOn, 52, true }, { &hpHz, 130 }, { &lpOn, 52 }, { &lpHz, 130 }, { &compOn, 62 }, { &thr, 124 }, { &ratio, 124 },
            { &grMeter, 34 }, { &presetBtn, 28 }, { &limOn, 50 }, { &ceil, 124 }, { &width, 130 } };
        bool first = true;
        for (auto* s : sends) { items.push_back({ s, 120, first }); first = false; }
        items.push_back({ &routeSummary, 180 });
        items.push_back({ &matrixBtn, 78 });
        return kit::flow(items, r, px(26), px(4), apply);
    }
    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    const int g;
    bool live = false;
    LevelMeter meterL, meterR;
    juce::Label nameLabel, memberSummary, routeSummary;
    ParamToggle mute, solo, pol, hpOn, lpOn, compOn, limOn;
    kit::Captioned trim, balance, delay, hpHz, lpHz, thr, ratio, ceil, width;
    juce::OwnedArray<kit::Captioned> sends;
    GainReductionMeter grMeter;
    juce::TextButton presetBtn { juce::String::fromUTF8("\xe2\x96\xbe") };
};

class GroupsPage final : public Page, private Tickable {
public:
    explicit GroupsPage(EditorContext& c) : Page(c) {
        kit::styleLabel(title, 16.0f, true, palette().accent);
        title.setText("Crowd groups", juce::dontSendNotification);
        kit::styleLabel(hint, 11.5f, false, palette().textMuted);
        hint.setText("Each group is a stereo submix: Inputs -> Group -> FX sends -> Master. Numbered cells = member inputs.", juce::dontSendNotification);
        addAndMakeVisible(title);
        addAndMakeVisible(hint);
        for (int g = 0; g < kMaxGroups; ++g) content.addAndMakeVisible(rows.add(new GroupRow(c, g)));
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);
    }
    void pageShown() override { live = true; for (auto* r : rows) { r->setLive(true); r->refresh(); } syncSelection(); }
    void pageHidden() override { live = false; for (auto* r : rows) r->setLive(false); }
    void tick() override {
        if (!live) return;
        const auto rev = proc.getRouting().revision();
        const int n = proc.getActiveInputChannelCount();
        if (rev != lastRev || n != lastInputs) {
            lastRev = rev;
            if (n != lastInputs) { lastInputs = n; layoutRows(); }
            for (auto* r : rows) r->refresh();
        }
    }
    void resized() override {
        auto r = getLocalBounds().reduced(px(8));
        auto top = r.removeFromTop(px(28));
        title.setBounds(top.removeFromLeft(px(170)));
        hint.setBounds(top);
        r.removeFromTop(px(4));
        viewport.setBounds(r);
        layoutRows();
    }
private:
    void selectionChanged() override { syncSelection(); }
    void showModelChanged() override { for (auto* r : rows) r->refresh(); }
    void syncSelection() {
        const auto s = ctx.getSelection();
        for (int g = 0; g < rows.size(); ++g) rows[g]->setSelected(s.kind == Selection::Kind::Group && s.index == g);
    }
    void layoutRows() {
        const int w = viewport.getWidth() - viewport.getScrollBarThickness() - 2;
        if (w <= 0) return;
        int y = 0;
        for (auto* row : rows) {
            const int h = row->layoutHeight(w);
            row->setBounds(0, y, w, h);
            y += h + px(4);
        }
        content.setSize(w, y);
    }
    juce::Label title, hint;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<GroupRow> rows;
    bool live = false;
    std::uint32_t lastRev = 0xffffffffu;
    int lastInputs = -1;
};
}
std::unique_ptr<Page> makeGroupsPage(EditorContext& c) { return std::make_unique<GroupsPage>(c); }
}
