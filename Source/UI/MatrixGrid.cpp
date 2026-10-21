#include "MatrixGrid.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include "UI/Theme.h"
#include <cmath>

namespace crowdmike::ui {
namespace {
float toDb(float g) { return juce::Decibels::gainToDecibels(std::abs(g), -100.0f); }
float fromDb(float db) { return juce::Decibels::decibelsToGain(db); }
bool isUnity(float g) { return std::abs(std::abs(g) - 1.0f) < 1.0e-3f; }
}

// ================================================================= level popup (non-modal callout)
class MatrixGrid::LevelPopup final : public juce::Component {
public:
    LevelPopup(MatrixGrid& g, int r, int c, bool numeric) : grid(&g), row(r), col(c) {
        startGain = g.gainAt(r, c);
        current = startGain;
        sign = startGain < 0.0f ? -1.0f : 1.0f;
        title.setText(g.describeCell(r, c), juce::dontSendNotification);
        title.setFont(font(12.0f, true));
        title.setColour(juce::Label::textColourId, palette().text);
        addAndMakeVisible(title);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, px(64), px(22));
        slider.setRange(static_cast<double>(g.minDb), static_cast<double>(juce::Decibels::gainToDecibels(g.maxGain)), 0.1);
        slider.setTextValueSuffix(" dB");
        slider.setValue(startGain == 0.0f ? -12.0 : static_cast<double>(toDb(startGain)), juce::dontSendNotification);
        slider.setTitle("Send level in dB");
        slider.setTooltip("Drag to set the level; type a value in the box (dB). Drag to the bottom for off.");
        slider.setWantsKeyboardFocus(true);
        slider.onValueChange = [this] {
            const float db = static_cast<float>(slider.getValue());
            apply(db <= grid->minDb + 0.05f ? 0.0f : sign * fromDb(db));
        };
        addAndMakeVisible(slider);
        offBtn.setTooltip("Turn this route off");
        unityBtn.setTooltip("Set exactly unity gain (0 dB)");
        offBtn.onClick = [this] { apply(0.0f); dismiss(); };
        unityBtn.onClick = [this] { apply(sign); dismiss(); };
        addAndMakeVisible(offBtn);
        addAndMakeVisible(unityBtn);
        setSize(px(300), px(84));
        if (numeric) juce::MessageManager::callAsync([sp = juce::Component::SafePointer<juce::Slider>(&slider)] { if (sp != nullptr) sp->showTextBox(); });
    }
    ~LevelPopup() override {
        if (grid != nullptr && std::abs(current - startGain) > 1.0e-6f && grid->commit) grid->commit(row, col, startGain, current);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(px(6));
        title.setBounds(r.removeFromTop(px(20)));
        auto bottom = r.removeFromBottom(px(26));
        offBtn.setBounds(bottom.removeFromLeft(bottom.getWidth() / 2).reduced(px(2), 0));
        unityBtn.setBounds(bottom.reduced(px(2), 0));
        slider.setBounds(r.reduced(0, px(2)));
    }
private:
    void apply(float g) {
        current = g;
        if (grid == nullptr) return;
        if (g != 0.0f) grid->remember(row, col, g);
        if (grid->setLive) grid->setLive(row, col, g);
        grid->repaint();
    }
    void dismiss() { if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss(); }
    juce::Component::SafePointer<MatrixGrid> grid;
    int row, col;
    float startGain = 0.0f, current = 0.0f, sign = 1.0f;
    juce::Label title;
    juce::Slider slider;
    juce::TextButton offBtn { "Off" }, unityBtn { "Unity 0 dB" };
};

// ================================================================= MatrixGrid
MatrixGrid::MatrixGrid() {
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
    setTitle("Routing matrix");
}
MatrixGrid::~MatrixGrid() { if (popupBox != nullptr) popupBox->dismiss(); }

void MatrixGrid::layoutRows() {
    const int n = rows();
    rowTop.assign(static_cast<size_t>(n), 0);
    int y = showColHeaders ? px(colHeaderH) : 0;
    juce::String prevSection;
    for (int r = 0; r < n; ++r) {
        if (rowSection) {
            const auto s = rowSection(r);
            if (s != prevSection && s.isNotEmpty()) y += px(sectionH);
            prevSection = s;
        }
        rowTop[static_cast<size_t>(r)] = y;
        y += px(cellH);
    }
    bodyBottom = y;
}

int MatrixGrid::getIdealWidth() const { return (showRowHeaders ? px(rowHeaderW) : 0) + cols() * px(cellW) + 1; }
int MatrixGrid::getIdealHeight() const {
    const_cast<MatrixGrid*>(this)->layoutRows();
    return bodyBottom + (footer ? px(cellH) + px(4) : 0) + 1;
}

juce::Rectangle<int> MatrixGrid::cellBounds(int r, int c) const {
    if (r < 0 || r >= static_cast<int>(rowTop.size()) || c < 0 || c >= cols()) return {};
    return { (showRowHeaders ? px(rowHeaderW) : 0) + c * px(cellW), rowTop[static_cast<size_t>(r)], px(cellW), px(cellH) };
}

MatrixGrid::Hit MatrixGrid::hitTest(juce::Point<int> p) const {
    Hit h;
    const int x0 = showRowHeaders ? px(rowHeaderW) : 0;
    const int col = p.x >= x0 ? (p.x - x0) / juce::jmax(1, px(cellW)) : -1;
    h.col = col < cols() ? col : -1;
    if (showColHeaders && p.y < px(colHeaderH)) { h.header = true; return h; }
    if (footer && p.y >= bodyBottom + px(4) && p.y < bodyBottom + px(4) + px(cellH)) { h.footerRow = true; return h; }
    for (int r = 0; r < static_cast<int>(rowTop.size()); ++r)
        if (p.y >= rowTop[static_cast<size_t>(r)] && p.y < rowTop[static_cast<size_t>(r)] + px(cellH)) { h.row = r; break; }
    if (p.x < x0) { h.header = h.row >= 0; h.col = -1; }
    return h;
}

bool MatrixGrid::isEditable(int r, int c) const {
    if (r < 0 || c < 0 || r >= rows() || c >= cols()) return false;
    if (exists && !exists(r, c)) return false;
    return !(blockReason && blockReason(r, c).isNotEmpty());
}

float MatrixGrid::rememberedGain(int r, int c) const {
    const size_t k = static_cast<size_t>(r * juce::jmax(1, cols()) + c);
    return k < lastOnGain.size() && lastOnGain[k] != 0.0f ? lastOnGain[k] : 1.0f;
}
void MatrixGrid::remember(int r, int c, float g) {
    const size_t k = static_cast<size_t>(r * juce::jmax(1, cols()) + c);
    if (lastOnGain.size() <= k) lastOnGain.resize(k + 1, 0.0f);
    lastOnGain[k] = g;
}

void MatrixGrid::setCell(int r, int c, float g) {
    if (!isEditable(r, c)) return;
    g = juce::jlimit(-maxGain, maxGain, g);
    const float old = gainAt(r, c);
    if (std::abs(old - g) < 1.0e-7f) return;
    if (g != 0.0f) remember(r, c, g);
    if (commit) commit(r, c, old, g);
    repaint();
}
void MatrixGrid::toggleCell(int r, int c) {
    const float g = gainAt(r, c);
    if (g != 0.0f) remember(r, c, g);
    setCell(r, c, g != 0.0f ? 0.0f : rememberedGain(r, c));
}
void MatrixGrid::toggleUnity(int r, int c) {
    const float g = gainAt(r, c);
    setCell(r, c, isUnity(g) ? 0.0f : (g < 0.0f ? -1.0f : 1.0f));
}
void MatrixGrid::nudgeCell(int r, int c, float db) {
    const float g = gainAt(r, c);
    if (g == 0.0f) { if (db > 0.0f) setCell(r, c, fromDb(-20.0f)); return; }
    const float ndb = toDb(g) + db;
    setCell(r, c, ndb < minDb ? 0.0f : std::copysign(juce::jmin(maxGain, fromDb(ndb)), g));
}
void MatrixGrid::setFocusCell(int r, int c) {
    focusRow = juce::jlimit(0, juce::jmax(0, rows() - 1), r);
    focusCol = juce::jlimit(0, juce::jmax(0, cols() - 1), c);
    if (auto* vp = findParentComponentOfClass<juce::Viewport>()) {
        const auto b = cellBounds(focusRow, focusCol);
        auto* content = vp->getViewedComponent();
        if (content != nullptr && !b.isEmpty()) {
            const auto inContent = content->getLocalArea(this, b);
            const auto view = vp->getViewArea();
            int vx = view.getX(), vy = view.getY();
            if (inContent.getX() < vx) vx = inContent.getX();
            else if (inContent.getRight() > view.getRight()) vx = inContent.getRight() - view.getWidth();
            if (inContent.getY() < vy) vy = inContent.getY();
            else if (inContent.getBottom() > view.getBottom()) vy = inContent.getBottom() - view.getHeight();
            vp->setViewPosition(vx, vy);
        }
    }
    if (onHover) onHover(focusRow, focusCol);
    repaint();
}

juce::String MatrixGrid::levelText(float g) const {
    if (g == 0.0f) return "off";
    const juce::String pol = g < 0.0f ? juce::String::fromUTF8("\xc3\x98 ") : juce::String();
    if (isUnity(g)) return pol + "0 dB (unity)";
    const float db = toDb(g);
    return pol + (db > 0.0f ? "+" : "") + juce::String(db, 1) + " dB";
}

juce::String MatrixGrid::describeCell(int r, int c) const {
    return (rowName ? rowName(r) : juce::String(r + 1)) + juce::String::fromUTF8(" \xe2\x86\x92 ") + (colName ? colName(c) : juce::String(c + 1));
}

juce::String MatrixGrid::getTooltip() {
    const auto pos = getMouseXYRelative();
    const auto h = hitTest(pos);
    if (h.footerRow && h.col >= 0 && footer) return footer(h.col).tip;
    if (h.header) {
        if (h.row >= 0) return rowTip ? rowTip(h.row) : (rowName ? rowName(h.row) : juce::String());
        if (h.col >= 0) return colTip ? colTip(h.col) : (colName ? colName(h.col) : juce::String());
        return {};
    }
    if (h.row < 0 || h.col < 0 || (exists && !exists(h.row, h.col))) return {};
    juce::String t = describeCell(h.row, h.col);
    if (blockReason) {
        const auto why = blockReason(h.row, h.col);
        if (why.isNotEmpty()) return t + "\nBLOCKED: " + why;
    }
    t << ": " << levelText(gainAt(h.row, h.col));
    if (isPre && gainAt(h.row, h.col) != 0.0f) t << (isPre(h.row, h.col) ? "  [PRE-fader]" : "  [post-fader]");
    if (cellInfo) { const auto extra = cellInfo(h.row, h.col); if (extra.isNotEmpty()) t << "\n" << extra; }
    t << "\nClick: on/off   Drag: level   Double-click: type value   Ctrl-click: unity   Right-click: menu";
    return t;
}

void MatrixGrid::paintCell(juce::Graphics& g, int r, int c, juce::Rectangle<int> b) {
    const auto& p = palette();
    const bool active = (!rowActive || rowActive(r)) && (!colActive || colActive(c));
    auto cell = b.reduced(1).toFloat();
    const float alpha = active ? 1.0f : 0.45f;
    const juce::String why = blockReason ? blockReason(r, c) : juce::String();
    if (why.isNotEmpty()) {
        g.setColour(p.background.withAlpha(alpha));
        g.fillRect(cell);
        g.saveState();
        g.reduceClipRegion(cell.toNearestInt());
        g.setColour(p.panelEdge.withAlpha(0.9f * alpha));
        for (float x = cell.getX() - cell.getHeight(); x < cell.getRight(); x += 6.0f)
            g.drawLine(x, cell.getBottom(), x + cell.getHeight(), cell.getY(), 1.0f);
        g.restoreState();
        g.setColour(p.textMuted.withAlpha(0.7f * alpha));
        g.setFont(font(11.0f, true));
        g.drawText(juce::String::fromUTF8("\xe2\x9c\x95"), cell, juce::Justification::centred);
        return;
    }
    const float gain = gainAt(r, c);
    const bool on = gain != 0.0f;
    const bool hl = cellHighlighted && cellHighlighted(r, c);
    g.setColour((on ? p.accentDim : p.background.brighter(0.04f)).withAlpha(alpha));
    g.fillRoundedRectangle(cell, 3.0f);
    if (on) {   // level bar along the bottom: -60..+6 dB
        const float prop = juce::jlimit(0.0f, 1.0f, (toDb(gain) - minDb) / (juce::Decibels::gainToDecibels(maxGain) - minDb));
        g.setColour(p.accent.withAlpha(alpha));
        g.fillRect(cell.getX() + 2.0f, cell.getBottom() - 4.0f, (cell.getWidth() - 4.0f) * prop, 2.5f);
    }
    g.setColour((hl ? p.accent : p.panelEdge).withAlpha(alpha));
    g.drawRoundedRectangle(cell, 3.0f, hl ? 2.0f : 1.0f);

    juce::String text;
    if (cellLabel) {
        text = cellLabel(r, c);
        g.setFont(font(11.5f, on));
        g.setColour((on ? p.text : p.textMuted).withAlpha(alpha));
        g.drawText(text, cell.withTrimmedBottom(3.0f), juce::Justification::centred);
        if (on) { g.setColour(p.text.withAlpha(alpha)); g.fillEllipse(cell.getRight() - 7.0f, cell.getY() + 3.0f, 4.0f, 4.0f); }
        if (on && !isUnity(gain)) { g.setFont(font(8.5f)); g.drawText(juce::String(juce::roundToInt(toDb(gain))), cell.reduced(2.0f), juce::Justification::topLeft); }
    } else {
        if (!on) text = juce::String::fromUTF8("\xc2\xb7");
        else if (isUnity(gain)) text = gain < 0.0f ? juce::String::fromUTF8("\xc3\x98\xe2\x97\x8f") : juce::String::fromUTF8("\xe2\x97\x8f");
        else {
            const float db = toDb(gain);
            text = (gain < 0.0f ? juce::String::fromUTF8("\xc3\x98") : juce::String()) + (db > 0.0f ? "+" : "") + juce::String(juce::roundToInt(db));
        }
        g.setFont(font(on ? 12.0f : 14.0f, on));
        g.setColour((on ? p.text : p.textMuted).withAlpha(alpha));
        g.drawText(text, cell.withTrimmedBottom(3.0f), juce::Justification::centred);
    }
    if (on && isPre && isPre(r, c)) {
        g.setFont(font(8.5f, true));
        g.setColour(p.warn.withAlpha(alpha));
        g.drawText("PRE", cell.reduced(2.0f, 1.0f), juce::Justification::topLeft);
    }
    if (riskLevel) {
        const int lvl = riskLevel(r, c);
        if (lvl > 0) {
            const auto col = lvl >= 2 ? p.danger : p.warn;
            juce::Path tri;
            const float s = juce::jmin(13.0f, cell.getHeight() * 0.55f);
            tri.addTriangle(cell.getRight() - s, cell.getY(), cell.getRight(), cell.getY(), cell.getRight(), cell.getY() + s);
            g.setColour(col);
            g.fillPath(tri);
            g.setColour(p.background);
            g.setFont(font(8.0f, true));
            g.drawText(lvl >= 2 ? "!!" : "!", juce::Rectangle<float>(cell.getRight() - s * 0.62f, cell.getY(), s * 0.62f, s * 0.6f), juce::Justification::centred);
        }
    }
    if (hasKeyboardFocus(false) && r == focusRow && c == focusCol) {
        g.setColour(p.text);
        const float dashes[] = { 3.0f, 2.0f };
        juce::Path outline;
        outline.addRoundedRectangle(cell.reduced(1.5f), 3.0f);
        juce::Path dashed;
        juce::PathStrokeType(1.5f).createDashedStroke(dashed, outline, dashes, 2);
        g.fillPath(dashed);
    }
}

void MatrixGrid::paint(juce::Graphics& g) {
    const auto& p = palette();
    if (static_cast<int>(rowTop.size()) != rows()) layoutRows();
    const int nR = rows(), nC = cols();
    const int x0 = showRowHeaders ? px(rowHeaderW) : 0;
    // column headers
    if (showColHeaders) {
        g.setFont(font(11.0f, true));
        for (int c = 0; c < nC; ++c) {
            const bool hl = colHighlighted && colHighlighted(c);
            const bool active = !colActive || colActive(c);
            auto hb = juce::Rectangle<int>(x0 + c * px(cellW), 0, px(cellW), px(colHeaderH)).reduced(1, 2);
            if (hl || c == hoverCol) { g.setColour(p.accentDim.withAlpha(hl ? 0.6f : 0.3f)); g.fillRoundedRectangle(hb.toFloat(), 3.0f); }
            g.setColour(active ? p.text : p.textMuted.withAlpha(0.7f));
            g.drawFittedText(colName ? colName(c) : juce::String(c + 1), hb.reduced(1, 0), juce::Justification::centredBottom, 3, 0.7f);
        }
    }
    // rows
    juce::String prevSection;
    for (int r = 0; r < nR; ++r) {
        const int y = rowTop[static_cast<size_t>(r)];
        if (rowSection) {
            const auto s = rowSection(r);
            if (s != prevSection && s.isNotEmpty()) {
                const int sy = y - px(sectionH);
                g.setColour(p.panelEdge);
                g.drawHorizontalLine(sy + px(sectionH) / 2, static_cast<float>(x0 > 0 ? 0 : 0), static_cast<float>(x0 + nC * px(cellW)));
                g.setColour(p.accent);
                g.setFont(font(10.5f, true));
                const auto label = s.toUpperCase();
                const int tw = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), label) + px(10);
                g.setColour(palette().background);
                g.fillRect(0, sy, tw, px(sectionH));
                g.setColour(p.accent);
                g.drawText(label, 2, sy, tw, px(sectionH), juce::Justification::centredLeft);
            }
            prevSection = s;
        }
        if (showRowHeaders) {
            const bool hl = rowHighlighted && rowHighlighted(r);
            const bool active = !rowActive || rowActive(r);
            auto hb = juce::Rectangle<int>(0, y, x0, px(cellH)).reduced(1, 1);
            if (hl || r == hoverRow) { g.setColour(p.accentDim.withAlpha(hl ? 0.6f : 0.3f)); g.fillRoundedRectangle(hb.toFloat(), 3.0f); }
            g.setColour(active ? p.text : p.textMuted.withAlpha(0.7f));
            g.setFont(font(12.0f, hl));
            g.drawFittedText((hl ? juce::String::fromUTF8("\xe2\x96\xb8 ") : juce::String()) + (rowName ? rowName(r) : juce::String(r + 1)),
                             hb.reduced(px(4), 0), juce::Justification::centredLeft, 1, 0.7f);
        }
        for (int c = 0; c < nC; ++c) {
            if (exists && !exists(r, c)) continue;
            paintCell(g, r, c, cellBounds(r, c));
        }
    }
    // footer
    if (footer) {
        const int fy = bodyBottom + px(4);
        g.setColour(p.panelEdge);
        g.drawHorizontalLine(fy - px(2), 0.0f, static_cast<float>(x0 + nC * px(cellW)));
        if (showRowHeaders) {
            g.setColour(p.textMuted);
            g.setFont(font(11.0f, true));
            g.drawFittedText(footerName, 4, fy, x0 - 8, px(cellH), juce::Justification::centredLeft, 2, 0.7f);
        }
        for (int c = 0; c < nC; ++c) {
            const auto f = footer(c);
            auto b = juce::Rectangle<int>(x0 + c * px(cellW), fy, px(cellW), px(cellH)).reduced(1);
            const auto col = f.level >= 2 ? p.danger : (f.level == 1 ? p.warn : p.textMuted);
            g.setColour(col.withAlpha(0.15f));
            g.fillRoundedRectangle(b.toFloat(), 3.0f);
            g.setColour(col);
            g.setFont(font(10.5f, f.level > 0));
            const juce::String glyph = f.level >= 2 ? juce::String::fromUTF8("\xe2\x9c\x95") : (f.level == 1 ? juce::String::fromUTF8("\xe2\x96\xb2") : juce::String());
            g.drawFittedText(glyph + f.text, b, juce::Justification::centred, 2, 0.6f);
        }
    }
}

void MatrixGrid::mouseMove(const juce::MouseEvent& e) {
    const auto h = hitTest(e.getPosition());
    const int r = h.footerRow ? -1 : h.row, c = h.col;
    if (r != hoverRow || c != hoverCol) {
        hoverRow = r; hoverCol = c;
        if (onHover) onHover(r, c);
        repaint();
    }
}
void MatrixGrid::mouseExit(const juce::MouseEvent&) {
    hoverRow = hoverCol = -1;
    if (onHover) onHover(-1, -1);
    repaint();
}

void MatrixGrid::mouseDown(const juce::MouseEvent& e) {
    dragging = false;
    dragRow = dragCol = -1;
    const auto h = hitTest(e.getPosition());
    if (h.header && h.row >= 0 && h.col < 0) { if (onRowClick) onRowClick(h.row); return; }
    if (h.header || h.footerRow || h.row < 0 || h.col < 0) return;
    focusRow = h.row; focusCol = h.col;
    repaint();
    if (!isEditable(h.row, h.col)) return;
    if (e.mods.isPopupMenu()) { showCellMenu(h.row, h.col); return; }
    dragRow = h.row; dragCol = h.col;
    dragStartGain = dragGain = gainAt(h.row, h.col);
}
void MatrixGrid::mouseDrag(const juce::MouseEvent& e) {
    if (dragRow < 0 || e.mods.isPopupMenu() || e.mods.isCommandDown() || e.mods.isCtrlDown()) return;
    if (!dragging && e.getDistanceFromDragStart() < 4) return;
    dragging = true;
    const float perPx = e.mods.isShiftDown() ? 0.05f : 0.25f;
    const float startDb = dragStartGain == 0.0f ? -20.0f : toDb(dragStartGain);
    const float db = startDb - static_cast<float>(e.getDistanceFromDragStartY()) * perPx;
    const float sign = dragStartGain < 0.0f ? -1.0f : 1.0f;
    dragGain = db < minDb ? 0.0f : sign * juce::jmin(maxGain, fromDb(db));
    if (setLive) setLive(dragRow, dragCol, dragGain);
    repaint();
}
void MatrixGrid::mouseUp(const juce::MouseEvent& e) {
    if (dragRow < 0) return;
    const int r = dragRow, c = dragCol;
    dragRow = dragCol = -1;
    if (dragging) {
        dragging = false;
        if (dragGain != 0.0f) remember(r, c, dragGain);
        if (commit && std::abs(dragGain - dragStartGain) > 1.0e-7f) commit(r, c, dragStartGain, dragGain);
        repaint();
        return;
    }
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) toggleUnity(r, c);
    else toggleCell(r, c);
}
void MatrixGrid::mouseDoubleClick(const juce::MouseEvent& e) {
    const auto h = hitTest(e.getPosition());
    if (!h.header && !h.footerRow && isEditable(h.row, h.col)) showLevelPopup(h.row, h.col, true);
}

void MatrixGrid::showLevelPopup(int r, int c, bool numeric) {
    if (!isEditable(r, c)) return;
    if (popupBox != nullptr) popupBox->dismiss();
    auto popup = std::make_unique<LevelPopup>(*this, r, c, numeric);
    auto area = getScreenBounds().getPosition() + cellBounds(r, c).getPosition();
    const auto target = juce::Rectangle<int>(area.x, area.y, px(cellW), px(cellH));
    if (getTopLevelComponent() != nullptr && getTopLevelComponent()->isOnDesktop())
        popupBox = &juce::CallOutBox::launchAsynchronously(std::move(popup), target, nullptr);
}

void MatrixGrid::showCellMenu(int r, int c) {
    juce::PopupMenu m;
    m.addSectionHeader(describeCell(r, c));
    const float g = gainAt(r, c);
    m.addItem(1, "Off", true, g == 0.0f);
    m.addItem(2, "Unity (0 dB)", true, isUnity(g) && g > 0.0f);
    const float levels[] = { -12.0f, -6.0f, -3.0f, 3.0f, 6.0f };
    for (int k = 0; k < 5; ++k) m.addItem(10 + k, (levels[k] > 0 ? "+" : "") + juce::String(juce::roundToInt(levels[k])) + " dB");
    m.addItem(3, "Enter level...");
    if (isPre && togglePre) { m.addSeparator(); m.addItem(4, "Pre-fader send", true, isPre(r, c)); }
    juce::Component::SafePointer<MatrixGrid> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(cellBounds(r, c))), [safe, r, c, levels](int res) {
        if (safe == nullptr || res <= 0) return;
        if (res == 1) safe->setCell(r, c, 0.0f);
        else if (res == 2) safe->setCell(r, c, 1.0f);
        else if (res == 3) safe->showLevelPopup(r, c, true);
        else if (res == 4) { safe->togglePre(r, c); safe->repaint(); }
        else if (res >= 10 && res < 15) safe->setCell(r, c, fromDb(levels[res - 10]));
    });
}

bool MatrixGrid::keyPressed(const juce::KeyPress& k) {
    const int code = k.getKeyCode();
    const bool shift = k.getModifiers().isShiftDown();
    if (code == juce::KeyPress::leftKey) { setFocusCell(focusRow, focusCol - 1); return true; }
    if (code == juce::KeyPress::rightKey) { setFocusCell(focusRow, focusCol + 1); return true; }
    if (code == juce::KeyPress::upKey) { setFocusCell(focusRow - 1, focusCol); return true; }
    if (code == juce::KeyPress::downKey) { setFocusCell(focusRow + 1, focusCol); return true; }
    if (code == juce::KeyPress::homeKey) { setFocusCell(focusRow, 0); return true; }
    if (code == juce::KeyPress::endKey) { setFocusCell(focusRow, cols() - 1); return true; }
    if (!isEditable(focusRow, focusCol)) return code == juce::KeyPress::spaceKey;
    const auto ch = k.getTextCharacter();
    if (code == juce::KeyPress::spaceKey) { toggleCell(focusRow, focusCol); return true; }
    if (ch == '+' || ch == '=' || code == juce::KeyPress::numberPadAdd) { nudgeCell(focusRow, focusCol, shift ? 0.25f : 1.0f); return true; }
    if (ch == '-' || ch == '_' || code == juce::KeyPress::numberPadSubtract) { nudgeCell(focusRow, focusCol, shift ? -0.25f : -1.0f); return true; }
    if (code == juce::KeyPress::returnKey) { showLevelPopup(focusRow, focusCol, true); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey || ch == '0') { setCell(focusRow, focusCol, 0.0f); return true; }
    if (ch == 'u' || ch == 'U') { setCell(focusRow, focusCol, 1.0f); return true; }
    if ((ch == 'p' || ch == 'P') && isPre && togglePre) { togglePre(focusRow, focusCol); repaint(); return true; }
    return false;
}
}
