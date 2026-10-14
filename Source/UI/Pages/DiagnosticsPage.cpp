// Diagnostics page: feedback view (GUI_SPEC section 11) + system health (CPU, overruns/xruns, recorder dropouts, route
// latency breakdown, output limiter gain reduction) and a "copy diagnostics to clipboard" button.
// The feedback analysis worker only runs while this page is visible (pageShown/pageHidden).
#include "UI/DiagnosticsReport.h"
#include "UI/FeedbackView.h"
#include "UI/LiveStrips.h"
#include "UI/Meters.h"
#include "UI/Pages.h"

namespace crowdmike::ui {
namespace {
class SystemPanel final : public juce::Component, private Tickable {
public:
    explicit SystemPanel(EditorContext& c) : ctx(c), proc(c.proc) {
        styleLiveLabel(title, 15.0f, true, palette().accent);
        title.setText("SYSTEM HEALTH", juce::dontSendNotification);
        copy.setTooltip("Copy the full diagnostics report (engine, latency, recorder, analysis, limiters) to the clipboard");
        copy.setTitle("Copy diagnostics to clipboard");
        copy.onClick = [this] {
            juce::SystemClipboard::copyTextToClipboard(buildDiagnosticsReport(proc));
            ctx.say("Diagnostics copied to the clipboard");
        };
        for (auto* b : { &cpuBadge, &overBadge, &dropBadge }) addAndMakeVisible(*b);
        addAndMakeVisible(title);
        addAndMakeVisible(copy);
        refresh();
    }
    void tick() override { if (isVisible() && ++count % 8 == 0) refresh(); }
    void refresh() {
        lines = diagnosticsLines(proc);
        auto& m = proc.getMeters();
        const int cpu = juce::roundToInt(m.cpuLoad.load() * 100.0f);
        cpuBadge.set("CPU " + juce::String(cpu) + "%", cpu >= 85 ? StatusBadge::Level::Danger : (cpu >= 60 ? StatusBadge::Level::Warn : StatusBadge::Level::Ok));
        int over = static_cast<int>(m.overruns.load());
        juce::String overText = "OVERRUN " + juce::String(over);
        if (auto* hs = proc.getHostServices()) { over += hs->getDeviceStatus().xruns; overText = "XRUN/OVR " + juce::String(over); }
        overBadge.set(overText, over > 0 ? StatusBadge::Level::Warn : StatusBadge::Level::Ok);
        const auto rs = proc.getRecorder().getStatus();
        dropBadge.set("REC DROP " + juce::String(rs.dropouts) + (rs.errors > 0 ? " ERR " + juce::String(rs.errors) : juce::String()),
                      rs.errors > 0 ? StatusBadge::Level::Danger : (rs.dropouts > 0 ? StatusBadge::Level::Warn : StatusBadge::Level::Ok));
        repaint(textArea);
    }
    void paint(juce::Graphics& g) override {
        const auto& p = palette();
        g.setColour(p.panel);
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 5.0f);
        auto r = textArea;
        const int lh = juce::jlimit(14, 22, juce::roundToInt(15.0f * scale()));
        for (const auto& l : lines) {
            if (r.getHeight() < lh) break;
            auto line = r.removeFromTop(lh);
            if (l.startsWith("==")) {
                g.setColour(p.accent);
                g.setFont(font(12.0f, true));
                g.drawText(l.substring(3).toUpperCase(), line.withTrimmedTop(2), juce::Justification::bottomLeft);
            } else {
                g.setColour(p.text);
                g.setFont(monoFont(11.5f));
                g.drawFittedText(l, line, juce::Justification::centredLeft, 1, 0.7f);
            }
        }
    }
    void resized() override {
        auto r = getLocalBounds().reduced(8);
        const int row = juce::jlimit(22, 30, r.getHeight() / 24);
        auto top = r.removeFromTop(row);
        copy.setBounds(top.removeFromRight(juce::jmin(200, top.getWidth() / 2)));
        title.setBounds(top);
        r.removeFromTop(4);
        auto badges = r.removeFromTop(row);
        const int bw = badges.getWidth() / 3;
        cpuBadge.setBounds(badges.removeFromLeft(bw).reduced(2, 0));
        overBadge.setBounds(badges.removeFromLeft(bw).reduced(2, 0));
        dropBadge.setBounds(badges.reduced(2, 0));
        r.removeFromTop(6);
        textArea = r;
    }
    juce::TextButton& copyButton() { return copy; }
private:
    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    juce::Label title;
    juce::TextButton copy { "Copy diagnostics" };
    StatusBadge cpuBadge, overBadge, dropBadge;
    juce::StringArray lines;
    juce::Rectangle<int> textArea;
    int count = 0;
};

class DiagnosticsPage final : public Page {
public:
    explicit DiagnosticsPage(EditorContext& c) : Page(c), feedback(c), system(c) {
        addAndMakeVisible(feedback);
        addAndMakeVisible(system);
    }
    void pageShown() override { feedback.setActive(true); system.refresh(); }
    void pageHidden() override { feedback.setActive(false); }
    void resized() override {
        auto r = getLocalBounds().reduced(6);
        const bool stacked = r.getWidth() < 900 && r.getHeight() > 700;
        if (stacked) {
            system.setBounds(r.removeFromBottom(r.getHeight() * 2 / 5));
            r.removeFromBottom(6);
        } else {
            system.setBounds(r.removeFromRight(juce::jlimit(330, 620, r.getWidth() * 2 / 5)));
            r.removeFromRight(6);
        }
        feedback.setBounds(r);
    }
private:
    FeedbackView feedback;
    SystemPanel system;
};
}
std::unique_ptr<Page> makeDiagnosticsPage(EditorContext& c) { return std::make_unique<DiagnosticsPage>(c); }
}
