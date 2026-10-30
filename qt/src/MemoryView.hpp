#pragma once
#include <QAbstractScrollArea>

#include "corrupt/memory_watch.h"

#include <functional>
#include <optional>
#include <utility>

// A hex view of one memory target, 16 bytes a row, with each byte's background showing
// how often the game changes it (Activity) or how often the recipe hit it (Hits). A
// strip on the right shows the whole target at once; click it to jump. Drag, or click
// and shift-click, to select a range. Only the visible rows are painted.
class MemoryView : public QAbstractScrollArea
{
    Q_OBJECT

  public:
    enum class Overlay
    {
        Activity,
        Hits,
        None,
    };

    explicit MemoryView(QWidget *parent = nullptr);

    // Keeps the selection and scroll position while the target and size stay the same.
    void setSnapshot(const corrupt::MemorySnapshot &snapshot);
    const corrupt::MemorySnapshot &snapshot() const { return snap; }
    void setOverlay(Overlay overlay);

    std::optional<std::pair<uint32_t, uint32_t>> selection() const; // inclusive, first <= second
    void select(uint32_t first, uint32_t last);                     // and scrolls it into view
    void clearSelection();
    void scrollTo(uint32_t offset);                                 // its row becomes the first one
    uint32_t firstVisibleOffset() const;

    // Geometry, in viewport coordinates.
    std::optional<uint32_t> offsetAt(QPoint pos) const;
    QRect cellRect(uint32_t offset) const; // empty when the cell isn't visible
    QRect overviewRect() const;

    std::function<void()> onSelectionChanged;

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
