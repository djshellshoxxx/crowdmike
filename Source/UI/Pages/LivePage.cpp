// Live page (GUI_SPEC section 3): the performance view.
//   top    : signal-flow ribbon Input -> Align -> Clean -> Shape -> FX -> Master -> Output (each stage navigates)
//   left   : horizontally scrollable input strips for the active inputs
//   middle : 8 crowd-group strips (one row of 8 or a 4x2 grid depending on the room)
//   right  : master section (intensity, width/correlation, combined waveform, FX activity, limiters, output bank)
// Layout is proportional so it works from 1100x650 to 4K. Meters only consume engine peaks while the page is shown.
#include "UI/DuckerStrip.h"
#include "UI/LiveStrips.h"
#include "UI/Pages.h"
#include "UI/ReportPanels.h"

namespace crowdmike::ui {
namespace {
class LivePage final : public Page, private Tickable {
public:
    explicit LivePage(EditorContext& c) : Page(c), ribbon(c), duck(c, live), master(c, live), energy(c.proc) {
        addAndMakeVisible(ribbon);
        addAndMakeVisible(duck);
        addAndMakeVisible(energy);
        inputViewport.setViewedComponent(&inputHolder, false);
        inputViewport.setScrollBarsShown(false, true);
        inputViewport.setTitle("Input strips");
        addAndMakeVisible(inputViewport);
        for (int g = 0; g < kMaxGroups; ++g) addAndMakeVisible(groups.add(new LiveGroupStrip(c, g, live)));
        addAndMakeVisible(master);
        for (auto* l : { &inputsTitle, &groupsTitle }) {
            styleLiveLabel(*l, 11.5f, true, palette().textMuted);
            addAndMakeVisible(*l);
        }
        groupsTitle.setText("CROWD GROUPS", juce::dontSendNotification);
        rebuildInputs();
    }
    ~LivePage() override { inputViewport.setViewedComponent(nullptr, false); }

    void pageShown() override {
        live.on = true;
        rebuildInputs();
        master.setNumOutputs(proc.getActiveOutputChannelCount());
        energy.setActive(true);
    }
    void pageHidden() override { live.on = false; energy.setActive(false); }

    void tick() override {
        // Channel layout can change while running (host re-prepare): follow it.
        if (live.on && (proc.getActiveInputChannelCount() != inputs.size()))
            rebuildInputs();
        if (live.on) master.setNumOutputs(proc.getActiveOutputChannelCount());
    }

    void resized() override {
        auto r = getLocalBounds().reduced(6);
        const int H = r.getHeight();
        auto top = r.removeFromTop(juce::jlimit(30, 48, H / 16));
        energy.setBounds(top.removeFromRight(juce::jlimit(170, 300, top.getWidth() / 5)).reduced(0, 2));   // Show Report sparkline
        top.removeFromRight(6);
        ribbon.setBounds(top);
        r.removeFromTop(4);
        duck.setBounds(r.removeFromTop(DuckerStrip::preferredHeight(r.getWidth())));
        r.removeFromTop(6);
        const int W = r.getWidth();
        auto masterArea = r.removeFromRight(juce::jlimit(190, 460, W / 5));
        r.removeFromRight(6);
        // Groups: one row of 8 when strips can be >= 64 px wide, else a 4x2 grid (when tall enough).
        const int groupW = juce::jlimit(8 * 52, 8 * 120, juce::roundToInt(W * 0.40f));
        auto groupArea = r.removeFromRight(groupW);
        r.removeFromRight(6);
        layoutGroups(groupArea);
        inputsTitle.setBounds(r.removeFromTop(16));
        inputViewport.setBounds(r);
        layoutInputs();
        master.setBounds(masterArea);
    }

private:
    void showModelChanged() override {
        for (auto* s : inputs) s->refreshNames();
        for (auto* s : groups) s->refreshNames();
    }

    void rebuildInputs() {
        const int n = juce::jlimit(1, kMaxInputs, proc.getActiveInputChannelCount());
        if (n == inputs.size()) return;
        inputs.clear();
        for (int i = 0; i < n; ++i) inputHolder.addAndMakeVisible(inputs.add(new LiveInputStrip(ctx, i, live)));
        inputsTitle.setText("INPUTS (" + juce::String(n) + " active)", juce::dontSendNotification);
        layoutInputs();
    }

    void layoutInputs() {
        if (inputs.isEmpty()) return;
        const int h = inputViewport.getHeight() - (inputViewport.isHorizontalScrollBarShown() ? inputViewport.getScrollBarThickness() : 0);
        const int avail = inputViewport.getWidth();
        const int stripW = juce::jlimit(juce::roundToInt(88 * scale()), juce::jmax(juce::roundToInt(150 * scale()), avail / 6), avail / inputs.size());
        const bool scroll = stripW * inputs.size() > avail;
        const int stripH = scroll ? inputViewport.getHeight() - inputViewport.getScrollBarThickness() : juce::jmax(h, inputViewport.getHeight());
        inputHolder.setSize(stripW * inputs.size(), juce::jmax(200, stripH));
        for (int i = 0; i < inputs.size(); ++i) inputs[i]->setBounds(i * stripW, 0, stripW - 4, inputHolder.getHeight());
    }

    void layoutGroups(juce::Rectangle<int> area) {
        groupsTitle.setBounds(area.removeFromTop(16));
        const bool grid = area.getWidth() / kMaxGroups < 64 && area.getHeight() >= 440;
        const int cols = grid ? 4 : kMaxGroups, rows = grid ? 2 : 1;
        const int w = area.getWidth() / cols, h = area.getHeight() / rows;
        for (int g = 0; g < groups.size(); ++g)
            groups[g]->setBounds(area.getX() + (g % cols) * w, area.getY() + (g / cols) * h, w - 3, h - (rows > 1 ? 4 : 0));
    }

    LiveFlag live;
    FlowRibbon ribbon;
    DuckerStrip duck;
    juce::Label inputsTitle, groupsTitle;
    juce::Viewport inputViewport;
    juce::Component inputHolder;
    juce::OwnedArray<LiveInputStrip> inputs;
    juce::OwnedArray<LiveGroupStrip> groups;
    LiveMasterSection master;
    EnergySparkline energy;
};
}
std::unique_ptr<Page> makeLivePage(EditorContext& c) { return std::make_unique<LivePage>(c); }
}
