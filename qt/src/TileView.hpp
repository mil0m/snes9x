#pragma once
#include <QAbstractScrollArea>

#include "MemoryPane.hpp"
#include "corrupt/tile_decode.h"

// One memory target decoded as SNES graphics tiles (2, 4 or 8 bpp, or mode 7) and coloured
// with the game's palette, so you see the graphics the way the game draws them. A row
// holds a chosen number of tiles; zoom sets the size. Selection is whole tiles: the range
// is every byte of the selected tiles. The overlays tint each tile by its busiest byte.
class TileView : public QAbstractScrollArea, public MemoryPane
{
    Q_OBJECT

  public:
    static constexpr int kGrey = -1; // palette row: show the raw colour indices
    static constexpr int kMinZoom = 1;
    static constexpr int kMaxZoom = 8;
    static constexpr int kMaxTilesPerRow = 64;

    explicit TileView(QWidget *parent = nullptr);

    void setSnapshot(const corrupt::MemorySnapshot &snapshot) override;
    const corrupt::MemorySnapshot &snapshot() const override { return snap; }
    void setOverlay(Overlay overlay) override;

    std::optional<std::pair<uint32_t, uint32_t>> selection() const override; // whole tiles, in bytes
    void select(uint32_t first, uint32_t last) override;
    void clearSelection() override;
    void scrollTo(uint32_t offset) override;
    uint32_t firstVisibleOffset() const override;

    corrupt::TileFormat format() const { return format_; }
    void setFormat(corrupt::TileFormat format); // keeps the place and the selected bytes
    void setCgram(const std::vector<uint8_t> &cgram);
    int paletteRow() const { return palette_row; }
    void setPaletteRow(int row); // 0 .. palette_count(format) - 1, or kGrey
    int tilesPerRow() const { return per_row; }
    void setTilesPerRow(int tiles);
    int zoom() const { return zoom_; } // screen pixels per tile pixel
    void setZoom(int zoom);
    void zoomIn();
    void zoomOut();

    uint32_t tileCount() const;
    uint32_t tileSpan() const { return corrupt::tile_span(format_); }

    // Geometry, in viewport coordinates.
    std::optional<uint32_t> tileAt(QPoint pos) const;
    QRect tileRect(uint32_t tile) const; // empty when the tile isn't fully visible

    std::function<void()> onZoomChanged;
    std::function<void(std::optional<uint32_t>)> onHover; // the tile under the mouse, or none

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
    int cell() const { return 8 * zoom_; }
    int rowCount() const;
    int visibleRows() const;
    int addressWidth() const;
    int imageLeft() const;
    void relayout(uint32_t keep_offset);
    void setSelectionTo(uint32_t anchor, uint32_t end);
    void zoomTo(int zoom, std::optional<QPoint> around);
    QRgb colour(uint8_t index) const;

    corrupt::MemorySnapshot snap;
    std::vector<uint8_t> cgram;
    Overlay overlay = Overlay::Activity;
    corrupt::TileFormat format_ = corrupt::TileFormat::Bpp4;
    std::optional<uint32_t> anchor; // bytes; widened to whole tiles
    std::optional<uint32_t> cursor;
    bool dragging = false;
    uint32_t max_hits = 1;
    int palette_row = 0;
    int per_row = 16;
    int zoom_ = 2;
};
