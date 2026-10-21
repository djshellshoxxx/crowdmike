#include "DuckerStrip.h"

namespace crowdmike::ui {
using namespace crowdmike::params;

namespace {
int didx(int e) { return ParamBank::index(Mod::Global, 0, duckBase() + e); }
std::unique_ptr<juce::Component> slider(CrowdMikeAudioProcessor& p, int e, int textW) {
    return std::make_unique<ParamSlider>(p, didx(e), juce::Slider::LinearHorizontal, textW);
}
}

DuckerStrip::DuckerStrip(EditorContext& c, const LiveFlag& f)
    : ctx(c), proc(c.proc), live(f),
      on(c.proc, didx(DuckP::On), "DUCK"), exclude(c.proc, didx(DuckP::ExcludeKey), "Excl key"),
      key(c.proc, didx(DuckP::Key)), target(c.proc, didx(DuckP::Target)),
      thr("Thr", slider(c.proc, DuckP::Thr, 52), 26), depth("Depth", slider(c.proc, DuckP::Depth, 52), 34),
      attack("Atk", slider(c.proc, DuckP::Attack, 52), 26), hold("Hold", slider(c.proc, DuckP::Hold, 52), 32),
      release("Rel", slider(c.proc, DuckP::Release, 56), 26) {
    setTitle("Performer ducker");
    setComponentID("ducker-strip");
    on.setComponentID("ducker-on");
    key.setComponentID("ducker-key");
    target.setComponentID("ducker-target");
    on.setTooltip("Performer ducker: while the key input is active the chosen buses are lowered by Depth (voice-activated)");
    key.setTooltip("Key input (post-trim, pre-processing). The performer's microphone or DI. None disables the ducker.");
    target.setTooltip("What is ducked: crowd groups, effect returns, or both. Recordings of inputs, groups and returns are never ducked.");
    exclude.setTooltip("Do not duck groups that the key input is assigned to");
    thr.setTooltip("Detector threshold (dBFS of the filtered key)");
    depth.setTooltip("Reduction applied while the key is active (0 .. -30 dB)");
    attack.setTooltip("Time to travel the whole depth when the key becomes active");
    hold.setTooltip("Time the ducker stays down after the key falls below threshold");
    release.setTooltip("Time to travel back up the whole depth");
    keyLevel.setTooltip("Key level (filtered). The marker shows the threshold. Click to clear the clip latch.");
    keyLevel.setTitle("Ducker key level");
    gr.setTitle("Ducker gain reduction");
    keyLevel.source = [this] { return live.on ? proc.getMeters().duckKeyPeak.getAndReset() : 0.0f; };
    gr.source = [this] { return live.on ? std::abs(proc.getMeters().duckGrDb.load(std::memory_order_relaxed)) : gr.shownDb(); };
    styleLiveLabel(status, 11.5f, false, palette().text);
    status.setFont(monoFont(11.5f));
    status.setTooltip("Ducker state: key level and applied gain reduction");
    for (juce::Component* comp : std::initializer_list<juce::Component*> { &on, &key, &exclude, &thr, &depth, &attack, &hold, &release, &target, &keyLevel, &gr, &status })
        addAndMakeVisible(comp);
}
DuckerStrip::~DuckerStrip() = default;

int DuckerStrip::preferredHeight() noexcept { return px(34); }
// Single row needs about 1350 px (controls 360 + 5 sliders x 150 + meters 220); narrower pages (compact mode, 1100 px windows)
// put the five sliders on their own second row so each keeps a usable track.
bool DuckerStrip::isStacked(int width) noexcept { return width < px(1350); }
int DuckerStrip::preferredHeight(int availableWidth) noexcept { return isStacked(availableWidth) ? px(34) + px(30) : px(34); }

void DuckerStrip::tick() {
    if (!live.on) return;
    const float t = kit::actual(proc, didx(DuckP::Thr));
    const bool changed = t != thresholdDb;
    thresholdDb = t;
    const auto& m = proc.getMeters();
    const float grDb = std::abs(m.duckGrDb.load(std::memory_order_relaxed));
    const bool open = m.duckOpen.load(std::memory_order_relaxed);
    const bool enabled = kit::isOn(proc, didx(DuckP::On));
    status.setText(juce::String(!enabled ? juce::String::fromUTF8("\xe2\x97\x8b off") : (open ? juce::String::fromUTF8("\xe2\x97\x8f DUCKING") : juce::String::fromUTF8("\xe2\x97\x8b armed")))
                       + "  GR " + juce::String(grDb, 1) + " dB",
                   juce::dontSendNotification);
    if (changed) repaint();
}

void DuckerStrip::paint(juce::Graphics& g) {
    const auto& p = palette();
    auto b = getLocalBounds().toFloat().reduced(1.5f);
    g.setColour(p.panel);
    g.fillRoundedRectangle(b, 5.0f);
    g.setColour(p.panelEdge);
    g.drawRoundedRectangle(b, 5.0f, 1.0f);
    // threshold marker over the key level meter (LevelMeter scale: -60 .. +3 dBFS)
    const auto km = keyLevel.getBounds().toFloat();
    if (km.getWidth() > 4.0f) {
        const float prop = juce::jlimit(0.0f, 1.0f, (thresholdDb + 60.0f) / 63.0f);
        g.setColour(p.text);
        g.fillRect(km.getX() + 1.5f + (km.getWidth() - 3.0f) * prop - 1.0f, km.getY() - 1.0f, 2.0f, km.getHeight() + 2.0f);
    }
}

void DuckerStrip::resized() {
    auto r = getLocalBounds().reduced(5, 3);
    const bool stacked = isStacked(getWidth());
    juce::Rectangle<int> sliderRow;
    if (stacked) { sliderRow = r.removeFromBottom(px(30) - 3); r.removeFromBottom(3); }
    const int h = r.getHeight();
    on.setBounds(r.removeFromLeft(px(64)));
    r.removeFromLeft(px(4));
    key.setBounds(r.removeFromLeft(px(86)).reduced(0, 1));
    r.removeFromLeft(px(4));
    target.setBounds(r.removeFromLeft(px(104)).reduced(0, 1));
    r.removeFromLeft(px(4));
    exclude.setBounds(r.removeFromLeft(px(78)));
    r.removeFromLeft(px(6));
    auto right = r.removeFromRight(px(220));
    status.setBounds(right.removeFromTop(h / 2));
    auto bars = right;
    keyLevel.setBounds(bars.removeFromTop(bars.getHeight() / 2).reduced(0, 1));
    gr.setBounds(bars.reduced(0, 1));
    auto& sliderArea = stacked ? sliderRow : r;
    const int w = sliderArea.getWidth() / 5;
    for (auto* c : { &thr, &depth, &attack, &hold, &release }) c->setBounds(sliderArea.removeFromLeft(w).reduced(px(2), 0));
}
}
