#include "ClipWaveformView.h"

namespace crowdmike::ui {
namespace { constexpr double kMinRegion = 0.002; }

ClipWaveformView::ClipWaveformView(bool isInteractive) : interactive(isInteractive) {
    setWantsKeyboardFocus(interactive);
    setInterceptsMouseClicks(interactive, false);
    if (interactive) {
        setTitle("Clip waveform and trim region");
        setTooltip("Drag the edges to trim the start / end, drag inside to move the region. "
                   "Keys: Left/Right move start, Shift+Left/Right move end (Alt = fine).");
    }
}

void ClipWaveformView::setEnvelope(const float* mn, const float* mx, int points) {
    if (mn == nullptr || mx == nullptr || points <= 0) { clearEnvelope(); return; }
    mins.assign(mn, mn + points);
    maxs.assign(mx, mx + points);
    repaint();
}

void ClipWaveformView::clearEnvelope() {
    if (mins.empty()) return;
    mins.clear();
    maxs.clear();
    repaint();
}

void ClipWaveformView::setTrim(double s, double e) {
    s = juce::jlimit(0.0, 1.0, s);
    e = juce::jlimit(s, 1.0, e);
    if (dragging != 0 || (std::abs(s - trimStart) < 1.0e-9 && std::abs(e - trimEnd) < 1.0e-9)) return;
    trimStart = s;
    trimEnd = e;
    repaint();
}

void ClipWaveformView::setPlayPosition(double p, bool isPlaying, bool isReversed) {
    if (std::abs(p - playPos) < 1.0e-4 && isPlaying == playing && isReversed == reversed) return;
    playPos = p;
    playing = isPlaying;
    reversed = isReversed;
    repaint();
}

double ClipWaveformView::xToFrac(float x) const {
    const float w = static_cast<float>(juce::jmax(1, getWidth() - 2));
    return juce::jlimit(0.0, 1.0, static_cast<double>((x - 1.0f) / w));
}
float ClipWaveformView::fracToX(double f) const {
    return 1.0f + static_cast<float>(f) * static_cast<float>(juce::jmax(1, getWidth() - 2));
}

void ClipWaveformView::paint(juce::Graphics& g) {
    const auto& p = palette();
    auto b = getLocalBounds().toFloat();
    g.setColour(p.background.darker(0.2f));
    g.fillRoundedRectangle(b, 3.0f);
    if (mins.empty()) {
        g.setColour(p.textMuted);
        g.setFont(font(interactive ? 13.0f : 11.0f));
        g.drawText(message.isNotEmpty() ? message : juce::String("No clip loaded"), getLocalBounds().reduced(4), juce::Justification::centred, true);
    } else {
        const float mid = b.getCentreY(), half = b.getHeight() * 0.46f;
        const int w = getWidth();
        const int n = static_cast<int>(mins.size());
        juce::Path wave;
        for (int x = 0; x < w; ++x) {
            const int i = juce::jlimit(0, n - 1, x * n / juce::jmax(1, w));
            const double f = static_cast<double>(x) / juce::jmax(1, w);
            const bool inside = f >= trimStart && f <= trimEnd;
            g.setColour(inside ? p.accent : p.textMuted.withAlpha(0.45f));
            const float top = mid - juce::jlimit(-1.0f, 1.0f, maxs[static_cast<size_t>(i)]) * half;
            const float bot = mid - juce::jlimit(-1.0f, 1.0f, mins[static_cast<size_t>(i)]) * half;
            g.drawVerticalLine(x, top, juce::jmax(top + 1.0f, bot));
        }
        // dim outside the trim region + handles
        g.setColour(p.background.withAlpha(0.45f));
        g.fillRect(b.withRight(fracToX(trimStart)));
        g.fillRect(b.withLeft(fracToX(trimEnd)));
        if (interactive) {
            g.setColour(p.text);
            for (double f : { trimStart, trimEnd }) {
                const float x = fracToX(f);
                g.fillRect(x - 1.0f, b.getY(), 2.0f, b.getHeight());
                juce::Path flag;
                const float dir = f == trimStart ? 1.0f : -1.0f;
                flag.addTriangle(x, b.getY(), x + 8.0f * dir, b.getY(), x, b.getY() + 10.0f);
                g.fillPath(flag);
            }
            g.setFont(font(11.0f));
            g.setColour(p.textMuted);
            g.drawText("S " + juce::String(trimStart * 100.0, 1) + "%", getLocalBounds().reduced(6, 2), juce::Justification::bottomLeft);
            g.drawText("E " + juce::String(trimEnd * 100.0, 1) + "%", getLocalBounds().reduced(6, 2), juce::Justification::bottomRight);
        }
        if (playing) {
            const double region = trimEnd - trimStart;
            const double pos = reversed ? trimEnd - playPos * region : trimStart + playPos * region;
            g.setColour(p.warn);
            g.fillRect(fracToX(pos) - 1.0f, b.getY(), 2.0f, b.getHeight());
        }
    }
    if (hasKeyboardFocus(false)) { g.setColour(p.accent); g.drawRoundedRectangle(b.reduced(0.5f), 3.0f, 1.5f); }
}

void ClipWaveformView::setTrimInternal(double s, double e, bool finished) {
    s = juce::jlimit(0.0, 1.0 - kMinRegion, s);
    e = juce::jlimit(s + kMinRegion, 1.0, e);
    trimStart = s;
    trimEnd = e;
    repaint();
    if (onTrimChanged) onTrimChanged(s, e, finished);
}

void ClipWaveformView::mouseMove(const juce::MouseEvent& e) {
    if (!interactive) return;
    const float xs = fracToX(trimStart), xe = fracToX(trimEnd);
    const bool nearEdge = std::abs(e.position.x - xs) < 6.0f || std::abs(e.position.x - xe) < 6.0f;
    setMouseCursor(nearEdge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
}

void ClipWaveformView::mouseDown(const juce::MouseEvent& e) {
    if (!interactive || mins.empty()) return;
    grabKeyboardFocus();
    const float xs = fracToX(trimStart), xe = fracToX(trimEnd);
    const float ds = std::abs(e.position.x - xs), de = std::abs(e.position.x - xe);
    if (juce::jmin(ds, de) < 8.0f) dragging = ds <= de ? 1 : 2;
    else if (e.position.x > xs && e.position.x < xe) dragging = 3;
    else dragging = e.position.x < xs ? 1 : 2;
    dragAnchor = xToFrac(e.position.x);
    dragStart0 = trimStart;
    dragEnd0 = trimEnd;
    mouseDrag(e);
}

void ClipWaveformView::mouseDrag(const juce::MouseEvent& e) {
    if (dragging == 0) return;
    const double f = xToFrac(e.position.x);
    if (dragging == 1) setTrimInternal(juce::jmin(f, trimEnd - kMinRegion), trimEnd, false);
    else if (dragging == 2) setTrimInternal(trimStart, juce::jmax(f, trimStart + kMinRegion), false);
    else {
        const double len = dragEnd0 - dragStart0;
        const double s = juce::jlimit(0.0, 1.0 - len, dragStart0 + (f - dragAnchor));
        setTrimInternal(s, s + len, false);
    }
}

void ClipWaveformView::mouseUp(const juce::MouseEvent&) {
    if (dragging == 0) return;
    dragging = 0;
    setTrimInternal(trimStart, trimEnd, true);
}

bool ClipWaveformView::keyPressed(const juce::KeyPress& k) {
    if (!interactive) return false;
    const int code = k.getKeyCode();
    if (code != juce::KeyPress::leftKey && code != juce::KeyPress::rightKey) return false;
    const double step = (k.getModifiers().isAltDown() ? 0.001 : 0.01) * (code == juce::KeyPress::leftKey ? -1.0 : 1.0);
    if (k.getModifiers().isShiftDown()) setTrimInternal(trimStart, trimEnd + step, true);
    else setTrimInternal(trimStart + step, trimEnd, true);
    return true;
}
}
