#include "TileView.hpp"

#include <QFontDatabase>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

using namespace corrupt;

namespace
{

QRgb blend(QRgb a, QRgb b, float t)
{
    auto ch = [t](int x, int y) { return int(x + (y - x) * t + 0.5f); };
    return qRgb(ch(qRed(a), qRed(b)), ch(qGreen(a), qGreen(b)), ch(qBlue(a), qBlue(b)));
}

} // namespace

TileView::TileView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    setFont(mono);
    viewport()->setFont(mono);
    viewport()->setMouseTracking(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), [this] { viewport()->update(); });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), [this] { viewport()->update(); });
}

int TileView::addressWidth() const
{
    // Room after the label for the row ticks and marks, so they never sit on the last digit.
    return fontMetrics().horizontalAdvance("0x" + QString(corrupt::address_digits(snap.target), QChar('0'))) + 20;
}

int TileView::imageLeft() const
{
    return addressWidth() - horizontalScrollBar()->value();
}

uint32_t TileView::tileCount() const
{
    return uint32_t((snap.bytes.size() + tileSpan() - 1) / tileSpan());
}

int TileView::rowCount() const
{
    return int((tileCount() + uint32_t(per_row) - 1) / uint32_t(per_row));
}

int TileView::visibleRows() const
{
    return std::max(1, viewport()->height() / cell());
}

QSize TileView::minimumSizeHint() const
{
    int frame = 2 * frameWidth() + verticalScrollBar()->sizeHint().width();
    return QSize(addressWidth() + 8 * 8 + frame, 120);
}

QSize TileView::sizeHint() const
{
    int frame = 2 * frameWidth() + verticalScrollBar()->sizeHint().width();
    return QSize(addressWidth() + 16 * 16 + frame, 512);
}

void TileView::relayout(uint32_t keep_offset)
{
    int room = std::max(1, viewport()->width() - addressWidth());
    horizontalScrollBar()->setRange(0, std::max(0, per_row * cell() - room));
    horizontalScrollBar()->setPageStep(room);
    horizontalScrollBar()->setSingleStep(cell());
    verticalScrollBar()->setRange(0, std::max(0, rowCount() - visibleRows()));
    verticalScrollBar()->setPageStep(visibleRows());
    verticalScrollBar()->setSingleStep(std::max(1, 24 / cell()));
    scrollTo(keep_offset);
    viewport()->update();
}

void TileView::setSnapshot(const MemorySnapshot &snapshot)
{
    bool same = snapshot.target == snap.target && snapshot.bytes.size() == snap.bytes.size();
    uint32_t keep = same ? firstVisibleOffset() : 0;
    snap = snapshot;
    scale = overlayScale(snap);
    if (!same)
    {
        anchor.reset();
        cursor.reset();
        dragging = false;
    }
    relayout(keep);
}

void TileView::setOverlay(Overlay o)
{
    overlay = o;
    viewport()->update();
}

void TileView::setFormat(TileFormat f)
{
    if (f == format_)
        return;
    uint32_t keep = firstVisibleOffset();
    format_ = f;
    palette_row = palette_row == kGrey ? kGrey : std::min(palette_row, palette_count(f) - 1);
    relayout(keep);
}

void TileView::setCgram(const std::vector<uint8_t> &colours)
{
    cgram = colours;
    viewport()->update();
}

void TileView::setPaletteRow(int row)
{
    palette_row = row < 0 ? kGrey : std::min(row, palette_count(format_) - 1);
    viewport()->update();
}

void TileView::setTilesPerRow(int tiles)
{
    tiles = std::clamp(tiles, 1, kMaxTilesPerRow);
    if (tiles == per_row)
        return;
    uint32_t keep = firstVisibleOffset();
    per_row = tiles;
    relayout(keep);
}

void TileView::setZoom(int zoom)
{
    zoomTo(zoom, std::nullopt);
}

void TileView::zoomIn()
{
    zoomTo(zoom_ + 1, std::nullopt);
}

void TileView::zoomOut()
{
    zoomTo(zoom_ - 1, std::nullopt);
}

void TileView::zoomTo(int zoom, std::optional<QPoint> around)
{
    zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    if (zoom == zoom_)
        return;
    std::optional<uint32_t> pinned = around ? tileAt(*around) : std::nullopt;
    zoom_ = zoom;
    relayout(firstVisibleOffset());
    if (pinned) // the tile under the mouse stays at the mouse's height
        verticalScrollBar()->setValue(int(*pinned / uint32_t(per_row)) - around->y() / cell());
    if (onZoomChanged)
        onZoomChanged();
}

uint32_t TileView::firstVisibleOffset() const
{
    return uint32_t(verticalScrollBar()->value()) * uint32_t(per_row) * tileSpan();
}

void TileView::scrollTo(uint32_t offset)
{
    verticalScrollBar()->setValue(int(offset / tileSpan() / uint32_t(per_row)));
}

QRect TileView::tileRect(uint32_t tile) const
{
    if (tile >= tileCount())
        return {};
    int row = int(tile / uint32_t(per_row)) - verticalScrollBar()->value();
    if (row < 0 || row >= visibleRows())
        return {};
    int x = imageLeft() + int(tile % uint32_t(per_row)) * cell();
    if (x < addressWidth() || x + cell() > viewport()->width())
        return {};
    return QRect(x, row * cell(), cell(), cell());
}

std::optional<uint32_t> TileView::tileAt(QPoint pos) const
{
    if (snap.bytes.empty() || pos.x() < addressWidth() || pos.y() < 0)
        return std::nullopt;
    int column = (pos.x() - imageLeft()) / cell();
    if (column >= per_row)
        return std::nullopt;
    uint32_t tile = uint32_t(pos.y() / cell() + verticalScrollBar()->value()) * uint32_t(per_row) + uint32_t(column);
    if (tile >= tileCount())
        return std::nullopt;
    return tile;
}

std::optional<std::pair<uint32_t, uint32_t>> TileView::selection() const
{
    if (!anchor || !cursor || snap.bytes.empty())
        return std::nullopt;
    uint32_t span = tileSpan();
    uint32_t first = std::min(*anchor, *cursor) / span * span;
    uint32_t last = std::max(*anchor, *cursor) / span * span + span - 1;
    return std::pair{ first, std::min<uint32_t>(last, uint32_t(snap.bytes.size() - 1)) };
}

void TileView::setSelectionTo(uint32_t a, uint32_t c)
{
    anchor = a;
    cursor = c;
    viewport()->update();
    if (onSelectionChanged)
        onSelectionChanged();
}

void TileView::select(uint32_t first, uint32_t last)
{
    if (snap.bytes.empty())
        return;
    last = std::min<uint32_t>(last, uint32_t(snap.bytes.size() - 1));
    first = std::min(first, last);
    uint32_t tile = first / tileSpan();
    if (tileRect(tile).isEmpty())
    {
        int row = int(tile / uint32_t(per_row));
        verticalScrollBar()->setValue(row - std::max(1, visibleRows() / 8));
        if (tileRect(tile).isEmpty()) // off to the side
            horizontalScrollBar()->setValue(int(tile % uint32_t(per_row)) * cell() - cell());
    }
    setSelectionTo(first, last);
}

void TileView::clearSelection()
{
    anchor.reset();
    cursor.reset();
    viewport()->update();
    if (onSelectionChanged)
        onSelectionChanged();
}

void TileView::resizeEvent(QResizeEvent *event)
{
    uint32_t keep = firstVisibleOffset();
    QAbstractScrollArea::resizeEvent(event);
    relayout(keep);
}

void TileView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    auto at = tileAt(event->position().toPoint());
    if (!at)
        return;
    uint32_t offset = *at * tileSpan();
    if (event->modifiers() & Qt::ShiftModifier && anchor)
        setSelectionTo(*anchor, offset);
    else
        setSelectionTo(offset, offset);
    dragging = true;
}

void TileView::mouseMoveEvent(QMouseEvent *event)
{
    QPoint pos = event->position().toPoint();
    auto at = tileAt(pos);
    if (onHover)
        onHover(at);
    if (!dragging || !anchor)
        return;
    if (pos.y() < 0)
        verticalScrollBar()->setValue(verticalScrollBar()->value() - 1);
    else if (pos.y() >= viewport()->height())
        verticalScrollBar()->setValue(verticalScrollBar()->value() + 1);
    if (at && *at * tileSpan() != cursor)
        setSelectionTo(*anchor, *at * tileSpan());
}

void TileView::mouseReleaseEvent(QMouseEvent *)
{
    dragging = false;
}

void TileView::wheelEvent(QWheelEvent *event)
{
    if (!(event->modifiers() & Qt::ControlModifier))
        return QAbstractScrollArea::wheelEvent(event);
    int delta = event->angleDelta().y();
    if (delta == 0)
        return;
    zoomTo(zoom_ + (delta > 0 ? 1 : -1), event->position().toPoint());
    event->accept();
}

bool TileView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::Leave && onHover)
        onHover(std::nullopt);
    return QAbstractScrollArea::viewportEvent(event);
}

QRgb TileView::colour(uint8_t index) const
{
    int colours = tile_colours(format_);
    if (palette_row == kGrey)
    {
        int g = index * 255 / (colours - 1);
        return qRgb(g, g, g);
    }
    Rgb c = palette_colour(cgram, palette_row * colours + index);
    return qRgb(c.r, c.g, c.b);
}

void TileView::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    // Around the picture is the window, not a page: a narrow picture doesn't sit beside a
    // white half (light) and the picture's own edge is the framed rectangle.
    QColor background = palette().color(QPalette::Window);
    p.fillRect(viewport()->rect(), background);
    if (snap.bytes.empty())
        return;

    int first_row = verticalScrollBar()->value();
    int rows = std::min(visibleRows() + 1, rowCount() - first_row);
    if (rows <= 0)
        return;

    // One image pixel per tile pixel for the visible tiles, then scaled up without smoothing.
    int first_col = horizontalScrollBar()->value() / cell();
    int cols = std::min(per_row - first_col, (viewport()->width() - addressWidth()) / cell() + 2);
    if (cols <= 0)
        return;
    QImage image(cols * 8, rows * 8, QImage::Format_RGB32);
    image.fill(background);
    QRgb hot = overlayColor(overlay).rgb();
    QRgb highlight = palette().color(QPalette::Highlight).rgb();
    auto sel = selection();
    uint32_t span = tileSpan();
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
        {
            uint32_t tile = uint32_t(first_row + r) * uint32_t(per_row) + uint32_t(first_col + c);
            if (tile >= tileCount())
                continue;
            size_t start = size_t(tile) * span;
            float level = 0.0f;
            if (overlay != Overlay::None)
                for (size_t i = start; i < std::min(snap.bytes.size(), start + span); i++)
                    level = std::max(level, overlayLevel(snap, overlay, i, scale));
            bool selected = sel && start >= sel->first && start <= sel->second;
            auto pixels = decode_tile(snap.bytes, start, format_);
            for (int y = 0; y < 8; y++)
            {
                auto line = reinterpret_cast<QRgb *>(image.scanLine(r * 8 + y)) + c * 8;
                for (int x = 0; x < 8; x++)
                {
                    QRgb px = colour(pixels[size_t(y * 8 + x)]);
                    if (level > 0.0f)
                        px = blend(px, hot, 0.25f + 0.5f * level);
                    if (selected)
                        px = blend(px, highlight, 0.3f);
                    line[x] = px;
                }
            }
        }
    p.drawImage(QRect(imageLeft() + first_col * cell(), 0, cols * cell(), rows * cell()), image);

    // Faint lines between tiles once they're big enough to need them.
    if (zoom_ >= 3)
    {
        QColor grid = palette().color(QPalette::Mid);
        grid.setAlpha(90);
        p.setPen(grid);
        for (int c = 1; c < cols; c++)
        {
            int x = imageLeft() + (first_col + c) * cell();
            p.drawLine(x, 0, x, rows * cell());
        }
        for (int r = 1; r < rows; r++)
            p.drawLine(imageLeft() + first_col * cell(), r * cell(), imageLeft() + (first_col + cols) * cell(), r * cell());
    }
    QColor frame = palette().color(QPalette::WindowText); // a quiet edge in light and dark
    frame.setAlphaF(0.25f);
    p.setPen(frame);
    p.drawRect(QRect(imageLeft(), 0, per_row * cell(), rows * cell()).adjusted(-1, -1, 0, 0));
    p.fillRect(QRect(0, 0, addressWidth() - 1, viewport()->height()), background);

    if (sel) // an outline around each row's run of selected tiles: a tint alone gets lost in graphics
    {
        uint32_t first_tile = sel->first / span, last_tile = sel->second / span;
        QPen outer(palette().color(QPalette::Highlight), 3), inner(palette().color(QPalette::HighlightedText), 1);
        p.setBrush(Qt::NoBrush);
        for (int r = 0; r < rows; r++)
        {
            uint32_t row_first = uint32_t(first_row + r) * uint32_t(per_row);
            uint32_t a = std::max(first_tile, row_first), b = std::min(last_tile, row_first + uint32_t(per_row) - 1);
            if (a > b)
                continue;
            int x0 = imageLeft() + int(a - row_first) * cell(), x1 = imageLeft() + int(b - row_first + 1) * cell();
            QRect run(x0 + 1, r * cell() + 1, x1 - x0 - 3, cell() - 3);
            p.setPen(outer); // a light line inside a dark one shows on any graphics
            p.drawRect(run);
            p.setPen(inner);
            p.drawRect(run.adjusted(2, 2, -2, -2));
        }
        p.fillRect(QRect(0, 0, addressWidth() - 1, viewport()->height()), background); // keep the gutter clean
    }

    if (sel) // a bar beside the selected rows, like the pixel view
    {
        int top = int(sel->first / span / uint32_t(per_row)) - first_row;
        int bottom = int(sel->second / span / uint32_t(per_row)) - first_row;
        if (bottom >= 0 && top < rows)
            p.fillRect(QRect(addressWidth() - 5, std::max(top, 0) * cell(), 3,
                             std::max(3, (std::min(bottom, rows - 1) - std::max(top, 0) + 1) * cell())),
                       palette().color(QPalette::Highlight));
    }

    int every = 1;
    while (every * cell() < fontMetrics().height() + 6)
        every *= 2;
    p.setPen(palette().color(QPalette::PlaceholderText));
    int ascent = fontMetrics().ascent();
    for (int r = 0; r < rows; r++)
    {
        if ((first_row + r) % every)
            continue;
        size_t start = size_t(first_row + r) * size_t(per_row) * span;
        p.drawText(4, r * cell() + ascent, QString::fromStdString(corrupt::hex_address(uint32_t(start), snap.target)));
    }
}
