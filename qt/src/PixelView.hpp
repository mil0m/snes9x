#pragma once
#include <QAbstractScrollArea>

#include "MemoryPane.hpp"

// One memory target with each byte drawn as a square pixel coloured by its value, so a
// whole memory fits on screen and fills, tables and graphics show as patterns. Zoom sets
// the pixel size; a row holds as many bytes as fit (a power of two). With the Activity or
// Hits overlay the bytes turn grey and the heat is painted over them. Drag, or click and
// shift-click, to select; Ctrl+wheel zooms around the mouse.
class PixelView : public QAbstractScrollArea, public MemoryPane
{
    Q_OBJECT

  public:
    static constexpr int kMinZoom = 1;
    static constexpr int kMaxZoom = 16;

    explicit PixelView(QWidget *parent = nullptr);

    void setSnapshot(const corrupt::MemorySnapshot &snapshot) override;
    const corrupt::MemorySnapshot &snapshot() const override { return snap; }
    void setOverlay(Overlay overlay) override;

    std::optional<std::pair<uint32_t, uint32_t>> selection() const override;
    void select(uint32_t first, uint32_t last) override;
    void clearSelection() override;
    void scrollTo(uint32_t offset) override;
    uint32_t firstVisibleOffset() const override;

    int zoom() const { return zoom_; } // screen pixels per byte, each way
    void setZoom(int zoom);            // keeps the first visible byte on screen
    void zoomIn();
    void zoomOut();
    int columns() const { return columns_; } // bytes per row

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
    void relayout(uint32_t keep_offset); // columns and scroll range for the zoom and width
    void setSelectionTo(uint32_t anchor, uint32_t end);
    void zoomTo(int zoom, std::optional<QPoint> around);

    corrupt::MemorySnapshot snap;
    Overlay overlay = Overlay::Activity;
    std::optional<uint32_t> anchor;
    std::optional<uint32_t> cursor;
    bool dragging = false;
    uint32_t max_hits = 1;
    int zoom_ = 2;
    int columns_ = 16;
};
