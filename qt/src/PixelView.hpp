#pragma once
#include <QAbstractScrollArea>

#include "MemoryPane.hpp"

// One memory target with each byte drawn as a square pixel coloured by its value, so a
// whole memory fits on screen and fills, tables and graphics show as patterns. Zoom sets
// the pixel size; a row holds as many bytes as fit (a power of two), or a chosen number
// of bytes, scrolling sideways when that's wider than the view. With the Activity or
// Hits overlay the bytes turn grey and the heat is painted over them. Drag, or click and
// shift-click, to select a range; Alt-drag selects a rectangle in the current row width;
// Ctrl+wheel zooms around the mouse.
class PixelView : public QAbstractScrollArea, public MemoryPane
{
    Q_OBJECT

  public:
    static constexpr int kMinZoom = 1;
    static constexpr int kMaxZoom = 16;
    static constexpr int kMaxRowWidth = 4096;

    explicit PixelView(QWidget *parent = nullptr);

    void setSnapshot(const corrupt::MemorySnapshot &snapshot) override;
    const corrupt::MemorySnapshot &snapshot() const override { return snap; }
    void setOverlay(Overlay overlay) override;

    std::optional<std::pair<uint32_t, uint32_t>> selection() const override; // a rectangle's bounding range

    // A rectangle selection, as drawn: top-left byte, the row width it was drawn in, size.
    struct RectSelection
    {
        uint32_t start = 0;
        uint32_t row = 1;
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t last() const { return start + (height - 1) * row + width - 1; }
        bool contains(uint32_t offset) const
        {
            return offset >= start && (offset - start) % row < width && (offset - start) / row < height;
        }
    };
    std::optional<RectSelection> rectSelection() const { return rect_sel; }
    void selectRect(uint32_t start, uint32_t width, uint32_t height); // in the current row width
    void select(uint32_t first, uint32_t last) override;
    void clearSelection() override;
    void scrollTo(uint32_t offset) override;
    uint32_t firstVisibleOffset() const override;

    int zoom() const { return zoom_; } // screen pixels per byte, each way
    void setZoom(int zoom);            // keeps the first visible byte on screen
    void zoomIn();
    void zoomOut();
    int columns() const { return columns_; } // bytes per row
    int rowWidth() const { return row_width_; } // the chosen bytes per row, 0 = as many as fit
    void setRowWidth(int bytes);                // keeps the first visible byte on screen

    // Geometry, in viewport coordinates.
    std::optional<uint32_t> offsetAt(QPoint pos) const;
    QRect pixelRect(uint32_t offset) const; // empty when the byte isn't visible

    std::function<void()> onZoomChanged;
    std::function<void(std::optional<uint32_t>)> onHover; // the byte under the mouse, or none

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

  protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool viewportEvent(QEvent *event) override;

  private:
    int rowCount() const;
    int visibleRows() const;
    int addressWidth() const;
    int imageLeft() const; // where column 0 is drawn, after scrolling sideways
    void relayout(uint32_t keep_offset); // columns and scroll range for the zoom and width
    void setSelectionTo(uint32_t anchor, uint32_t end);
    void zoomTo(int zoom, std::optional<QPoint> around);

    corrupt::MemorySnapshot snap;
    Overlay overlay = Overlay::Activity;
    std::optional<uint32_t> anchor;
    std::optional<uint32_t> cursor;
    std::optional<RectSelection> rect_sel;
    bool dragging = false;
    bool rect_drag = false;
    void setRectFrom(uint32_t a, uint32_t c); // the rectangle with these two corners
    OverlayScale scale;
    int zoom_ = 1; // the whole of WRAM fits in the window
    int columns_ = 16;
    int row_width_ = 0;
};
