#include "ReverseViz.h"
#include "UI/Theme.h"
#include <cmath>

namespace crowdmike::ui {
namespace {
float wrap01(float v) { v = std::fmod(v, 1.0f); return v < 0.0f ? v + 1.0f : v; }
// Calls fn(x0, x1) for the one or two pieces of a wrapped [start, start+len) region of a 0..1 ring.
template <typename Fn> void forRegion(float start, float len, Fn&& fn) {
    len = juce::jlimit(0.0f, 1.0f, len);
    if (len <= 0.0f) return;
    start = wrap01(start);
    const float end = start + len;
    if (end <= 1.0f) fn(start, end);
    else { fn(start, 1.0f); fn(0.0f, end - 1.0f); }
}
}

ReverseVizView::ReverseVizView() {
    setTitle("Reverse chunk buffer");
    setTooltip("Reverse Chunk capture buffer (circular). IN = incoming write head, PLAY = backward play head, filled = stored "
               "chunk, hatched = crossfade zones, dashed = what a trigger would capture now.");
}

juce::String ReverseVizView::stateName(int state, bool pending) {
    juce::String s;
    switch (state) {
        case 1: s = "ARMED"; break;
        case 2: s = juce::String::fromUTF8("PLAYING \xe2\x97\x80 backward"); break;
        case 3: s = "CROSSFADING"; break;
        default: s = "IDLE (capturing)"; break;
    }
    if (pending) s << juce::String::fromUTF8("  \xe2\x96\xb2 PENDING TRIGGER");
    return s;
}

void ReverseVizView::tick() {
    if (active && !active()) return;
    bool changed = false;
    if (source) {
        const auto v = source();
        changed = std::abs(v.writeHead01 - viz.writeHead01) > 1.0e-4f || std::abs(v.playHead01 - viz.playHead01) > 1.0e-4f
                  || v.state != viz.state || v.pending != viz.pending || std::abs(v.wet01 - viz.wet01) > 1.0e-3f
                  || std::abs(v.storedStart01 - viz.storedStart01) > 1.0e-4f || std::abs(v.storedLen01 - viz.storedLen01) > 1.0e-4f;
        viz = v;
    }
    if (chunkSeconds) { const float c = chunkSeconds(); changed = changed || c != lastChunk; lastChunk = c; }
    if (changed) repaint();
}

void ReverseVizView::paint(juce::Graphics& g) {
    const auto& p = palette();
    auto area = getLocalBounds().reduced(px(4));
    g.setColour(p.background.darker(0.2f));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);

    // header: state + wet amount (text, never colour only)
    auto header = area.removeFromTop(px(18));
    g.setFont(font(12.0f, true));
    g.setColour(viz.state >= 2 ? p.accent : (viz.pending ? p.warn : p.textMuted));
    g.drawText(stateName(viz.state, viz.pending), header, juce::Justification::centredLeft, true);
    g.setColour(p.text);
    g.drawText("WET " + juce::String(juce::roundToInt(juce::jlimit(0.0f, 1.0f, viz.wet01) * 100.0f)) + "%", header, juce::Justification::centredRight);

    // wet bar on the right
    auto wetBar = area.removeFromRight(px(12)).reduced(0, px(2));
    area.removeFromRight(px(6));
    g.setColour(p.panelEdge);
    g.fillRoundedRectangle(wetBar.toFloat(), 3.0f);
    const float wetH = wetBar.getHeight() * juce::jlimit(0.0f, 1.0f, viz.wet01);
    g.setColour(p.accent);
    g.fillRoundedRectangle(wetBar.toFloat().removeFromBottom(wetH), 3.0f);

    // time axis at the bottom (seconds of buffer position)
    auto axis = area.removeFromBottom(px(14));
    auto bar = area.reduced(0, px(10)).toFloat();
    const auto xOf = [&](float pos01) { return bar.getX() + juce::jlimit(0.0f, 1.0f, pos01) * bar.getWidth(); };

    g.setColour(p.panelRaised);
    g.fillRoundedRectangle(bar, 3.0f);
    g.setFont(font(9.5f));
    for (int s = 0; s <= static_cast<int>(capacitySeconds); s += 2) {
        const float x = xOf(static_cast<float>(s) / capacitySeconds);
        g.setColour(p.panelEdge);
        g.drawVerticalLine(juce::roundToInt(x), bar.getY(), bar.getBottom());
        g.setColour(p.textMuted);
        g.drawText(juce::String(s) + "s", juce::Rectangle<float>(x - 14.0f, static_cast<float>(axis.getY()), 28.0f, static_cast<float>(axis.getHeight())),
                   juce::Justification::centred);
    }

    // stored chunk + crossfade zones
    forRegion(viz.storedStart01, viz.storedLen01, [&](float a, float b) {
        g.setColour(p.accentDim);
        g.fillRect(juce::Rectangle<float>(xOf(a), bar.getY(), xOf(b) - xOf(a), bar.getHeight()));
    });
    if (viz.storedLen01 > 0.0f && viz.crossfade01 > 0.0f) {
        const float xf = juce::jmin(viz.crossfade01, viz.storedLen01 * 0.5f);
        for (const float start : { viz.storedStart01, viz.storedStart01 + viz.storedLen01 - xf })
            forRegion(start, xf, [&](float a, float b) {
                const auto zone = juce::Rectangle<float>(xOf(a), bar.getY(), juce::jmax(1.0f, xOf(b) - xOf(a)), bar.getHeight());
                g.saveState();
                g.reduceClipRegion(zone.toNearestInt().expanded(1, 0));
                g.setColour(p.warn.withAlpha(0.65f));
                for (float x = zone.getX() - zone.getHeight(); x < zone.getRight(); x += 4.0f)
                    g.drawLine(x, zone.getBottom(), x + zone.getHeight(), zone.getY(), 1.0f);
                g.restoreState();
            });
    }
    if (viz.storedLen01 > 0.0f) {
        g.setColour(p.text);
        g.setFont(font(10.0f, true));
        const float labelX = xOf(wrap01(viz.storedStart01));
        g.drawText("stored " + juce::String(viz.storedLen01 * capacitySeconds, 2) + " s", juce::Rectangle<float>(labelX + 3.0f, bar.getY() + 2.0f, 110.0f, 12.0f),
                   juce::Justification::centredLeft);
    }

    // next capture preview (what a trigger would freeze now)
    if (chunkSeconds) {
        const float len = juce::jlimit(0.0f, 1.0f, chunkSeconds() / capacitySeconds);
        forRegion(viz.writeHead01 - len, len, [&](float a, float b) {
            juce::Path outline;
            outline.addRectangle(xOf(a), bar.getY() + 1.0f, xOf(b) - xOf(a), bar.getHeight() - 2.0f);
            juce::Path dashed;
            const float dashes[] = { 4.0f, 3.0f };
            juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
            g.setColour(p.text.withAlpha(0.6f));
            g.fillPath(dashed);
        });
    }

    // write head
    const float wx = xOf(wrap01(viz.writeHead01));
    g.setColour(p.text);
    g.fillRect(wx - 1.0f, bar.getY() - static_cast<float>(px(8)), 2.0f, bar.getHeight() + static_cast<float>(px(8)));
    g.setFont(font(10.0f, true));
    const bool labelRight = wx - 40.0f < bar.getX();
    g.drawText(labelRight ? juce::String::fromUTF8("\xe2\x97\x82 IN") : juce::String::fromUTF8("IN \xe2\x96\xb8"),
               juce::Rectangle<float>(labelRight ? wx + 3.0f : wx - 40.0f, bar.getY() - static_cast<float>(px(10)), 38.0f, static_cast<float>(px(10))),
               labelRight ? juce::Justification::centredLeft : juce::Justification::centredRight);

    // backward play head
    if (viz.state >= 2) {
        const float px0 = xOf(wrap01(viz.playHead01));
        g.setColour(p.accent);
        g.fillRect(px0 - 1.5f, bar.getY(), 3.0f, bar.getHeight() + static_cast<float>(px(4)));
        juce::Path arrow;
        const float ay = bar.getBottom() + static_cast<float>(px(2));
        arrow.addTriangle(px0 - 9.0f, ay, px0, ay - 5.0f, px0, ay + 5.0f);
        g.fillPath(arrow);
        g.setFont(font(10.0f, true));
        g.drawText("PLAY", juce::Rectangle<float>(px0 + 3.0f, bar.getBottom() - 12.0f, 40.0f, 11.0f), juce::Justification::centredLeft);
    }
    if (viz.pending) {
        g.setColour(p.warn);
        g.drawRoundedRectangle(bar.expanded(1.5f), 3.0f, 2.0f);
    }
}
}
