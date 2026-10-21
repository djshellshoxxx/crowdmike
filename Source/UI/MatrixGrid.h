#pragma once
// Reusable routing grid (GUI_SPEC section 7, ROUTING_SPEC): rows x cols cells with off / unity / adjustable level
// states, a level popup (drag, numeric entry), blocked (illegal) cells drawn hatched with an explanatory tooltip,
// optional pre/post indicator, risk badge, path highlighting and a summary footer row (headroom estimate).
// Data is supplied through callbacks so one component serves the input->group grid, the output matrix, the
// group-membership toggles and the inputs page's group-assignment cells.
//
// Mouse: click toggles off <-> on, vertical drag adjusts level (Shift = fine), double-click opens numeric entry,
// Ctrl/Cmd-click toggles exactly unity, right-click opens a level menu. Keyboard: arrows move the focus cell,
// Space toggles, +/- adjust 1 dB (Shift 0.25 dB), Enter opens the level popup, Delete/0 = off, U = unity, P = pre/post.
// State is always shown with text/glyphs as well as colour.
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace crowdmike::ui {
class MatrixGrid : public juce::Component, public juce::TooltipClient {
public:
    struct Footer { juce::String text; int level = 0; juce::String tip; };   // level 0 ok, 1 caution, 2 danger

    MatrixGrid();
    ~MatrixGrid() override;

    // ---- data callbacks (numRows/numCols/getGain/commit are required)
    std::function<int()> numRows, numCols;
    std::function<juce::String(int)> rowName, colName, rowTip, colTip;
    std::function<juce::String(int)> rowSection;                 // a header band is drawn where the section changes
    std::function<bool(int, int)> exists;                        // false: no cell (e.g. ragged membership grids)
    std::function<float(int, int)> getGain;                      // linear gain (0 = off)
    std::function<void(int, int, float)> setLive;                // during a drag / popup (no undo); optional
    std::function<void(int, int, float oldGain, float newGain)> commit;   // final edit (records undo)
    std::function<juce::String(int, int)> blockReason;           // non-empty: illegal cell (hatched, not editable)
    std::function<int(int, int)> riskLevel;                      // 0 none, 1 caution, 2 high (badge)
    std::function<juce::String(int, int)> cellInfo;              // extra tooltip line
    std::function<bool(int, int)> isPre;                         // pre/post indicator where meaningful
    std::function<void(int, int)> togglePre;
    std::function<juce::String(int, int)> cellLabel;             // text drawn in the cell (membership numbers)
    std::function<Footer(int)> footer;                           // per-column summary row
    juce::String footerName;
    std::function<bool(int)> rowActive, colActive;               // inactive -> dimmed
    std::function<void(int row, int col)> onHover;               // -1 for "none"; row-only / col-only for headers
    std::function<void(int row)> onRowClick;                     // row header clicked
    std::function<bool(int, int)> cellHighlighted;               // path highlight
    std::function<bool(int)> rowHighlighted, colHighlighted;

    float maxGain = 2.0f;           // +6 dB
    float minDb = -60.0f;           // drag floor; below this the cell turns off

    // ---- geometry
    void setShowHeaders(bool rows, bool cols) { showRowHeaders = rows; showColHeaders = cols; resized(); }
    void setCellSize(int w, int h) { cellW = w; cellH = h; resized(); }
    void setHeaderSizes(int rowHeaderWidth, int colHeaderHeight) { rowHeaderW = rowHeaderWidth; colHeaderH = colHeaderHeight; resized(); }
    int getIdealWidth() const;
    int getIdealHeight() const;
    juce::Rectangle<int> cellBounds(int r, int c) const;

    // ---- actions (used by mouse/keyboard; public for tests)
    void toggleCell(int r, int c);
    void toggleUnity(int r, int c);
    void nudgeCell(int r, int c, float db);
    void setCell(int r, int c, float gain);
    void setFocusCell(int r, int c);
    int getFocusRow() const noexcept { return focusRow; }
    int getFocusCol() const noexcept { return focusCol; }
    bool isEditable(int r, int c) const;
    void showLevelPopup(int r, int c, bool numericEntry);
    void refresh() { repaint(); }

    // ---- juce::Component
    void paint(juce::Graphics&) override;
    void resized() override { layoutRows(); }
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    juce::String getTooltip() override;

private:
    struct Hit { int row = -1, col = -1; bool header = false, footerRow = false; };
    Hit hitTest(juce::Point<int>) const;
    void layoutRows();
    int rows() const { return numRows ? numRows() : 0; }
    int cols() const { return numCols ? numCols() : 0; }
    float gainAt(int r, int c) const { return getGain ? getGain(r, c) : 0.0f; }
    juce::String levelText(float g) const;
    juce::String describeCell(int r, int c) const;
    void showCellMenu(int r, int c);
    void paintCell(juce::Graphics&, int r, int c, juce::Rectangle<int>);

    bool showRowHeaders = true, showColHeaders = true;
    int cellW = 46, cellH = 26, rowHeaderW = 150, colHeaderH = 40, sectionH = 18;
    std::vector<int> rowTop;          // y of each row (sections insert bands)
    int bodyBottom = 0;
    int focusRow = 0, focusCol = 0;
    int hoverRow = -1, hoverCol = -1;
    // drag state
    int dragRow = -1, dragCol = -1;
    float dragStartGain = 0.0f, dragGain = 0.0f;
    bool dragging = false;
    std::vector<float> lastOnGain;     // remembered level per cell for toggling back on
    float rememberedGain(int r, int c) const;
    void remember(int r, int c, float g);

    class LevelPopup;
    juce::Component::SafePointer<juce::CallOutBox> popupBox;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MatrixGrid)
};
}
