#pragma once
#include <QAbstractScrollArea>

#include "MemoryPane.hpp"

// A hex view of one memory target, 16 bytes a row, with each byte's background showing
// how often the game changes it (Activity) or how often the recipe hit it (Hits). A
// strip on the right shows the whole target at once; click it to jump. Drag, or click
// and shift-click, to select a range. Only the visible rows are painted.
class MemoryView : public QAbstractScrollArea, public MemoryPane
{
    Q_OBJECT

  public:
    explicit MemoryView(QWidget *parent = nullptr);

    void setSnapshot(const corrupt::MemorySnapshot &snapshot) override;
    const corrupt::MemorySnapshot &snapshot() const override { return snap; }
    void setOverlay(Overlay overlay) override;

    std::optional<std::pair<uint32_t, uint32_t>> selection() const override;
    void select(uint32_t first, uint32_t last) override;
    void clearSelection() override;
    void scrollTo(uint32_t offset) override;
    uint32_t firstVisibleOffset() const override;

    // Geometry, in viewport coordinates.
    std::optional<uint32_t> offsetAt(QPoint pos) const;
    QRect cellRect(uint32_t offset) const; // empty when the cell isn't visible
    QRect overviewRect() const;

    QSize minimumSizeHint() const override; // the whole grid and the strip, side by side
    QSize sizeHint() const override;

  protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

  private:
    static constexpr int kColumns = 16;
    static constexpr int kOverviewWidth = 18;

    int rowCount() const;
    int visibleRows() const;
    int addressWidth() const;
    void updateScrollBar();
    void rebuildOverview();
    float level(size_t offset) const; // 0..1 for the current overlay
    QColor heatColor(float level) const;
    void setSelectionTo(uint32_t anchor, uint32_t end);

    corrupt::MemorySnapshot snap;
    Overlay overlay = Overlay::Activity;
    std::optional<uint32_t> anchor;
    std::optional<uint32_t> cursor;
    bool dragging = false;
    std::vector<float> overview; // one level per pixel row of the strip
    uint32_t max_hits = 1;
    int row_height = 16;
    int cell_width = 24;
};
