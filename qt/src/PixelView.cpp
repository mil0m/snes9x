#include "PixelView.hpp"
#include "corrupt/byte_colour.h"

#include <QFontDatabase>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include <array>

using namespace corrupt;

namespace
{

constexpr std::array<int, 8> kZoomSteps = { 1, 2, 3, 4, 6, 8, 12, 16 };

int floorPowerOfTwo(int n)
{
    int p = 1;
    while (p * 2 <= n)
        p *= 2;
    return p;
}

QRgb blend(QRgb a, QRgb b, float t)
{
    auto ch = [t](int x, int y) { return int(x + (y - x) * t + 0.5f); };
    return qRgb(ch(qRed(a), qRed(b)), ch(qGreen(a), qGreen(b)), ch(qBlue(a), qBlue(b)));
}

QRgb rgb(Rgb c)
{
    return qRgb(c.r, c.g, c.b);
}

} // namespace

PixelView::PixelView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    setFont(mono);
    viewport()->setFont(mono);
    viewport()->setMouseTracking(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded); // only a chosen row width can be too wide
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);    // a steady width, so the columns don't jump
    connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), [this] { viewport()->update(); });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), [this] { viewport()->update(); });
}

int PixelView::addressWidth() const
{
    return fontMetrics().horizontalAdvance("0x00000") + 10;
}

int PixelView::imageLeft() const
{
    return addressWidth() - horizontalScrollBar()->value();
}

int PixelView::rowCount() const
{
    return int((snap.bytes.size() + size_t(columns_) - 1) / size_t(columns_));
}

int PixelView::visibleRows() const
{
    return std::max(1, viewport()->height() / zoom_);
}

QSize PixelView::minimumSizeHint() const
{
    int frame = 2 * frameWidth() + verticalScrollBar()->sizeHint().width();
    return QSize(addressWidth() + 16 * 4 + frame, 120);
}

QSize PixelView::sizeHint() const
{
    int frame = 2 * frameWidth() + verticalScrollBar()->sizeHint().width();
    return QSize(addressWidth() + 512 + frame, 512);
}

void PixelView::relayout(uint32_t keep_offset)
{
    int room = std::max(1, viewport()->width() - addressWidth());
    if (row_width_ > 0)
        columns_ = row_width_;
    else
    {
        int cols = std::max(16, floorPowerOfTwo(room / zoom_));
        if (!snap.bytes.empty())
        {
            // No wider than a square: a small target gets rows rather than one thin line (cgram
            // comes out one 16-colour palette a row, oam eight sprite entries a row).
            int square = 16;
            while (size_t(square) * size_t(square) < snap.bytes.size())
                square *= 2;
            cols = std::min(cols, square);
        }
        columns_ = cols;
    }
    horizontalScrollBar()->setRange(0, std::max(0, columns_ * zoom_ - room));
    horizontalScrollBar()->setPageStep(room);
    horizontalScrollBar()->setSingleStep(std::max(8, 8 * zoom_));
    verticalScrollBar()->setRange(0, std::max(0, rowCount() - visibleRows()));
    verticalScrollBar()->setPageStep(visibleRows());
    verticalScrollBar()->setSingleStep(std::max(1, 24 / zoom_));
    verticalScrollBar()->setValue(int(keep_offset / uint32_t(columns_)));
    viewport()->update();
}

void PixelView::setRowWidth(int bytes)
{
    bytes = bytes <= 0 ? 0 : std::min(bytes, kMaxRowWidth);
    if (bytes == row_width_)
        return;
    uint32_t keep = firstVisibleOffset();
    row_width_ = bytes;
    relayout(keep);
}

void PixelView::setSnapshot(const MemorySnapshot &snapshot)
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

void PixelView::setOverlay(Overlay o)
{
    overlay = o;
    viewport()->update();
}

uint32_t PixelView::firstVisibleOffset() const
{
    return uint32_t(verticalScrollBar()->value()) * uint32_t(columns_);
}

void PixelView::scrollTo(uint32_t offset)
{
    verticalScrollBar()->setValue(int(offset / uint32_t(columns_)));
}

void PixelView::setZoom(int zoom)
{
    zoomTo(zoom, std::nullopt);
}

void PixelView::zoomIn()
{
    for (int z : kZoomSteps)
        if (z > zoom_)
            return zoomTo(z, std::nullopt);
}

void PixelView::zoomOut()
{
    for (auto it = kZoomSteps.rbegin(); it != kZoomSteps.rend(); ++it)
        if (*it < zoom_)
            return zoomTo(*it, std::nullopt);
}

void PixelView::zoomTo(int zoom, std::optional<QPoint> around)
{
    zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    if (zoom == zoom_)
        return;
    std::optional<uint32_t> pinned = around ? offsetAt(*around) : std::nullopt;
    zoom_ = zoom;
    relayout(firstVisibleOffset());
    if (pinned) // the byte under the mouse stays at the mouse's height
        verticalScrollBar()->setValue(int(*pinned / uint32_t(columns_)) - around->y() / zoom_);
    if (onZoomChanged)
        onZoomChanged();
}

QRect PixelView::pixelRect(uint32_t offset) const
{
    if (offset >= snap.bytes.size())
        return {};
    int row = int(offset / uint32_t(columns_)) - verticalScrollBar()->value();
    if (row < 0 || row >= visibleRows())
        return {};
    int x = imageLeft() + int(offset % uint32_t(columns_)) * zoom_;
    if (x < addressWidth() || x + zoom_ > viewport()->width())
        return {}; // scrolled off sideways
    return QRect(x, row * zoom_, zoom_, zoom_);
}

std::optional<uint32_t> PixelView::offsetAt(QPoint pos) const
{
    if (snap.bytes.empty() || pos.x() < addressWidth() || pos.y() < 0)
        return std::nullopt;
    int column = (pos.x() - imageLeft()) / zoom_;
    if (column >= columns_)
        return std::nullopt;
    size_t offset = size_t(pos.y() / zoom_ + verticalScrollBar()->value()) * size_t(columns_) + size_t(column);
    if (offset >= snap.bytes.size())
        return std::nullopt;
    return uint32_t(offset);
}

std::optional<std::pair<uint32_t, uint32_t>> PixelView::selection() const
{
    if (!anchor || !cursor)
        return std::nullopt;
    return std::pair{ std::min(*anchor, *cursor), std::max(*anchor, *cursor) };
}

void PixelView::setSelectionTo(uint32_t a, uint32_t c)
{
    anchor = a;
    cursor = c;
    viewport()->update();
    if (onSelectionChanged)
        onSelectionChanged();
}

void PixelView::select(uint32_t first, uint32_t last)
{
    if (snap.bytes.empty())
        return;
    last = std::min<uint32_t>(last, uint32_t(snap.bytes.size() - 1));
    first = std::min(first, last);
    if (pixelRect(first).isEmpty())
    {
        uint32_t context = uint32_t(std::max(2, visibleRows() / 8) * columns_); // a little above it
        scrollTo(first >= context ? first - context : 0);
    }
    if (pixelRect(first).isEmpty()) // off to the side
        horizontalScrollBar()->setValue(int(first % uint32_t(columns_)) * zoom_ - 8 * zoom_);
    setSelectionTo(first, last);
}

void PixelView::clearSelection()
{
    anchor.reset();
    cursor.reset();
    viewport()->update();
    if (onSelectionChanged)
        onSelectionChanged();
}

void PixelView::resizeEvent(QResizeEvent *event)
{
    uint32_t keep = firstVisibleOffset();
    QAbstractScrollArea::resizeEvent(event);
    relayout(keep);
}

void PixelView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    auto at = offsetAt(event->position().toPoint());
    if (!at)
        return;
    if (event->modifiers() & Qt::ShiftModifier && anchor)
        setSelectionTo(*anchor, *at);
    else
        setSelectionTo(*at, *at);
    dragging = true;
}

void PixelView::mouseMoveEvent(QMouseEvent *event)
{
    QPoint pos = event->position().toPoint();
    auto at = offsetAt(pos);
    if (onHover)
        onHover(at);
    if (!dragging || !anchor)
        return;
    if (pos.y() < 0)
        verticalScrollBar()->setValue(verticalScrollBar()->value() - 1);
    else if (pos.y() >= viewport()->height())
        verticalScrollBar()->setValue(verticalScrollBar()->value() + 1);
    if (at && at != cursor)
        setSelectionTo(*anchor, *at);
}

void PixelView::mouseReleaseEvent(QMouseEvent *)
{
    dragging = false;
}

void PixelView::wheelEvent(QWheelEvent *event)
{
    if (!(event->modifiers() & Qt::ControlModifier))
        return QAbstractScrollArea::wheelEvent(event);
    int delta = event->angleDelta().y();
    if (delta == 0)
        return;
    int next = zoom_;
    if (delta > 0)
    {
        for (int z : kZoomSteps)
            if (z > zoom_)
            {
                next = z;
                break;
            }
    }
    else
    {
        for (auto it = kZoomSteps.rbegin(); it != kZoomSteps.rend(); ++it)
            if (*it < zoom_)
            {
                next = *it;
                break;
            }
    }
    zoomTo(next, event->position().toPoint());
    event->accept();
}

bool PixelView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::Leave && onHover)
        onHover(std::nullopt);
    return QAbstractScrollArea::viewportEvent(event);
}

void PixelView::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    QColor background = palette().color(QPalette::Base);
    p.fillRect(viewport()->rect(), background);
    if (snap.bytes.empty())
        return;

    int first_row = verticalScrollBar()->value();
    int rows = std::min(visibleRows() + 1, rowCount() - first_row);
    if (rows <= 0)
        return;

    // One image pixel per visible byte, then scaled up without smoothing.
    int first_col = horizontalScrollBar()->value() / zoom_;
    int cols = std::min(columns_ - first_col, (viewport()->width() - addressWidth()) / zoom_ + 2);
    if (cols <= 0)
        return;
    QImage image(cols, rows, QImage::Format_RGB32);
    QRgb empty = background.rgb();
    QRgb hot = overlayColor(overlay).rgb();
    QRgb highlight = palette().color(QPalette::Highlight).rgb();
    auto sel = selection();
    for (int r = 0; r < rows; r++)
    {
        auto line = reinterpret_cast<QRgb *>(image.scanLine(r));
        size_t start = size_t(first_row + r) * size_t(columns_) + size_t(first_col);
        for (int c = 0; c < cols; c++)
        {
            size_t offset = start + size_t(c);
            if (offset >= snap.bytes.size())
            {
                line[c] = empty;
                continue;
            }
            QRgb colour;
            if (overlay == Overlay::None)
                colour = rgb(byte_colour(snap.bytes[offset]));
            else
            {
                colour = rgb(byte_grey(snap.bytes[offset]));
                if (float t = overlayLevel(snap, overlay, offset, scale); t > 0.0f)
                    colour = blend(colour, hot, 0.2f + 0.8f * t);
            }
            if (sel && offset >= sel->first && offset <= sel->second)
                colour = blend(colour, highlight, 0.45f);
            line[c] = colour;
        }
    }
    QRect area(imageLeft() + first_col * zoom_, 0, cols * zoom_, rows * zoom_);
    p.drawImage(area, image);
    p.setPen(palette().color(QPalette::Mid)); // so a white fill doesn't melt into the page
    p.drawRect(QRect(imageLeft(), 0, columns_ * zoom_, rows * zoom_).adjusted(-1, -1, 0, 0));

    // Hot blocks (what Avoid dangerous bytes skips), on the crash map overlays: outlined
    // from 4x up, and a tick beside their rows at any zoom (at 1x a block is a dot or two).
    std::vector<int> hot_rows;
    if (showsHot(overlay) && !snap.hot_blocks.empty())
    {
        constexpr size_t kBlock = corrupt::CrashMap::kBlock;
        p.setPen(QColor(255, 255, 255));
        p.setBrush(Qt::NoBrush);
        for (int r = 0; r < rows; r++)
        {
            size_t row_start = size_t(first_row + r) * size_t(columns_);
            size_t row_end = std::min(snap.bytes.size(), row_start + size_t(columns_));
            bool any = false;
            for (size_t block = row_start / kBlock; block * kBlock < row_end; block++)
            {
                if (block >= snap.hot_blocks.size() || !snap.hot_blocks[block])
                    continue;
                any = true;
                if (zoom_ < 4)
                    break;
                int first = int(std::max(row_start, block * kBlock) - row_start);
                int last = int(std::min(row_end, (block + 1) * kBlock) - row_start); // past the end
                p.drawRect(QRect(imageLeft() + first * zoom_, r * zoom_, (last - first) * zoom_ - 1, zoom_ - 1));
            }
            if (any)
                hot_rows.push_back(r);
        }
    }

    p.fillRect(QRect(0, 0, addressWidth() - 1, viewport()->height()), background); // rows scroll under it
    int tick = std::max(3, zoom_);
    for (int r : hot_rows)
        p.fillRect(QRect(addressWidth() - 12, r * zoom_ + zoom_ / 2 - tick / 2, 3, tick), QColor(225, 40, 40));

    // A bar beside the rows the selection covers: at 1:1 a selection can be a pixel high.
    if (sel)
    {
        int top = int(sel->first / uint32_t(columns_)) - first_row;
        int bottom = int(sel->second / uint32_t(columns_)) - first_row;
        if (bottom >= 0 && top < rows)
        {
            top = std::max(top, 0);
            bottom = std::min(bottom, rows - 1);
            p.fillRect(QRect(addressWidth() - 5, top * zoom_, 3, std::max(3, (bottom - top + 1) * zoom_)),
                       palette().color(QPalette::Highlight));
        }
    }

    // Addresses every few rows, far enough apart to read.
    int every = 1;
    while (every * zoom_ < fontMetrics().height() + 8)
        every *= 2;
    p.setPen(palette().color(QPalette::PlaceholderText));
    int ascent = fontMetrics().ascent();
    for (int r = 0; r < rows; r++)
    {
        if ((first_row + r) % every)
            continue;
        int y = r * zoom_;
        size_t start = size_t(first_row + r) * size_t(columns_);
        p.drawText(4, y + ascent, QString("0x%1").arg(start, 5, 16, QChar('0')).toUpper().replace("0X", "0x"));
        p.drawLine(addressWidth() - 9, y, addressWidth() - 7, y);
    }
}
