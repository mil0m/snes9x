#include "MemoryView.hpp"

#include <QFontDatabase>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>

#include <algorithm>
#include <cmath>

using namespace corrupt;

MemoryView::MemoryView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    setFont(mono);
    viewport()->setFont(mono);
    QFontMetrics fm(mono);
    row_height = fm.height() + 2;
    cell_width = fm.horizontalAdvance("00") + 8;
    verticalScrollBar()->setSingleStep(1);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), [this] { viewport()->update(); });
}

int MemoryView::rowCount() const
{
    return int((snap.bytes.size() + kColumns - 1) / kColumns);
}

int MemoryView::visibleRows() const
{
    return std::max(1, viewport()->height() / row_height);
}

int MemoryView::addressWidth() const
{
    return fontMetrics().horizontalAdvance("0x00000") + 12;
}

QSize MemoryView::minimumSizeHint() const
{
    int grid = addressWidth() + kColumns * cell_width + 6;
    int frame = 2 * frameWidth() + verticalScrollBar()->sizeHint().width();
    return QSize(grid + 10 + kOverviewWidth + frame, row_height * 8);
}

QSize MemoryView::sizeHint() const
{
    return QSize(minimumSizeHint().width(), row_height * 24);
}

void MemoryView::updateScrollBar()
{
    verticalScrollBar()->setRange(0, std::max(0, rowCount() - visibleRows()));
    verticalScrollBar()->setPageStep(visibleRows());
}

void MemoryView::setSnapshot(const MemorySnapshot &snapshot)
{
    bool same = snapshot.target == snap.target && snapshot.bytes.size() == snap.bytes.size();
    snap = snapshot;
    max_hits = maxHits(snap);
    if (!same)
    {
        anchor.reset();
        cursor.reset();
        dragging = false;
    }
    updateScrollBar();
    if (!same)
        verticalScrollBar()->setValue(0);
    rebuildOverview();
    viewport()->update();
}

void MemoryView::setOverlay(Overlay o)
{
    overlay = o;
    rebuildOverview();
    viewport()->update();
}

float MemoryView::level(size_t offset) const
{
    return overlayLevel(snap, overlay, offset, max_hits);
}

QColor MemoryView::heatColor(float t) const
{
    QColor base = palette().color(QPalette::Base);
    return mix(base, overlayColor(overlay), t);
}

void MemoryView::rebuildOverview()
{
    QRect strip = overviewRect();
    overview.assign(size_t(std::max(0, strip.height())), 0.0f);
    if (snap.bytes.empty() || overview.empty())
        return;
    size_t per = std::max<size_t>(1, (snap.bytes.size() + overview.size() - 1) / overview.size());
    for (size_t y = 0; y < overview.size(); y++)
    {
        float best = 0.0f;
        for (size_t i = y * per; i < std::min(snap.bytes.size(), (y + 1) * per); i++)
            best = std::max(best, level(i));
        overview[y] = best;
    }
}

QRect MemoryView::overviewRect() const
{
    if (snap.bytes.empty())
        return {};
    return QRect(viewport()->width() - kOverviewWidth, 0, kOverviewWidth, viewport()->height());
}

uint32_t MemoryView::firstVisibleOffset() const
{
    return uint32_t(verticalScrollBar()->value()) * kColumns;
}

void MemoryView::scrollTo(uint32_t offset)
{
    verticalScrollBar()->setValue(int(offset / kColumns));
}

QRect MemoryView::cellRect(uint32_t offset) const
{
    if (offset >= snap.bytes.size())
        return {};
    int row = int(offset / kColumns) - verticalScrollBar()->value();
    if (row < 0 || row >= visibleRows())
        return {};
    int column = int(offset % kColumns);
    int x = addressWidth() + column * cell_width + (column >= 8 ? 6 : 0);
    return QRect(x, row * row_height, cell_width, row_height);
}

std::optional<uint32_t> MemoryView::offsetAt(QPoint pos) const
{
    if (snap.bytes.empty() || pos.x() < addressWidth() || pos.y() < 0)
        return std::nullopt;
    int x = pos.x() - addressWidth();
    int column;
    if (x < 8 * cell_width)
        column = x / cell_width;
    else if (x >= 8 * cell_width + 6)
        column = 8 + (x - 8 * cell_width - 6) / cell_width;
    else
        return std::nullopt; // the gap between the two halves of a row
    if (column >= kColumns)
        return std::nullopt;
    int row = pos.y() / row_height + verticalScrollBar()->value();
    size_t offset = size_t(row) * kColumns + size_t(column);
    if (offset >= snap.bytes.size())
        return std::nullopt;
    return uint32_t(offset);
}

std::optional<std::pair<uint32_t, uint32_t>> MemoryView::selection() const
{
    if (!anchor || !cursor)
        return std::nullopt;
    return std::pair{ std::min(*anchor, *cursor), std::max(*anchor, *cursor) };
}

void MemoryView::setSelectionTo(uint32_t a, uint32_t c)
{
    anchor = a;
    cursor = c;
    viewport()->update();
    if (onSelectionChanged)
        onSelectionChanged();
}

void MemoryView::select(uint32_t first, uint32_t last)
{
    if (snap.bytes.empty())
        return;
    last = std::min<uint32_t>(last, uint32_t(snap.bytes.size() - 1));
    first = std::min(first, last);
    if (cellRect(first).isEmpty())
        scrollTo(first >= kColumns * 2 ? first - kColumns * 2 : 0); // a little context above
    setSelectionTo(first, last);
}

void MemoryView::clearSelection()
{
    anchor.reset();
    cursor.reset();
    viewport()->update();
    if (onSelectionChanged)
        onSelectionChanged();
}

void MemoryView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBar();
    rebuildOverview();
}

void MemoryView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    QPoint pos = event->position().toPoint();
    if (overviewRect().contains(pos))
    {
        double fraction = double(pos.y()) / std::max(1, overviewRect().height());
        int row = int(fraction * rowCount()) - visibleRows() / 2;
        verticalScrollBar()->setValue(std::max(0, row));
        return;
    }
    auto at = offsetAt(pos);
    if (!at)
        return;
    if (event->modifiers() & Qt::ShiftModifier && anchor)
        setSelectionTo(*anchor, *at);
    else
        setSelectionTo(*at, *at);
    dragging = true;
}

void MemoryView::mouseMoveEvent(QMouseEvent *event)
{
    if (!dragging || !anchor)
        return;
    QPoint pos = event->position().toPoint();
    if (pos.y() < 0)
        verticalScrollBar()->setValue(verticalScrollBar()->value() - 1);
    else if (pos.y() >= viewport()->height())
        verticalScrollBar()->setValue(verticalScrollBar()->value() + 1);
    if (auto at = offsetAt(pos); at && at != cursor)
        setSelectionTo(*anchor, *at);
}

void MemoryView::mouseReleaseEvent(QMouseEvent *)
{
    dragging = false;
}

void MemoryView::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    p.fillRect(viewport()->rect(), palette().color(QPalette::Base));
    if (snap.bytes.empty())
        return;

    QColor text = palette().color(QPalette::Text);
    QColor dim = palette().color(QPalette::PlaceholderText);
    QColor highlight = palette().color(QPalette::Highlight);
    auto sel = selection();
    int first_row = verticalScrollBar()->value();
    int ascent = fontMetrics().ascent() + 1;

    for (int r = 0; r < visibleRows() + 1; r++)
    {
        size_t start = size_t(first_row + r) * kColumns;
        if (start >= snap.bytes.size())
            break;
        int y = r * row_height;
        p.setPen(dim);
        p.drawText(4, y + ascent, QString("0x%1").arg(start, 5, 16, QChar('0')).toUpper().replace("0X", "0x"));
        for (int c = 0; c < kColumns && start + size_t(c) < snap.bytes.size(); c++)
        {
            uint32_t offset = uint32_t(start) + uint32_t(c);
            QRect cell = cellRect(offset);
            if (cell.isEmpty())
                continue;
            bool selected = sel && offset >= sel->first && offset <= sel->second;
            float t = level(offset);
            if (selected)
                p.fillRect(cell, highlight);
            else if (t > 0.0f)
                p.fillRect(cell.adjusted(1, 1, -1, -1), heatColor(t));
            p.setPen(selected ? palette().color(QPalette::HighlightedText) : text);
            p.drawText(cell, Qt::AlignCenter, QString("%1").arg(snap.bytes[offset], 2, 16, QChar('0')).toUpper());
        }
    }

    // The whole target at a glance, with the visible part marked.
    QRect strip = overviewRect();
    p.fillRect(strip, palette().color(QPalette::AlternateBase));
    p.setPen(palette().color(QPalette::Mid));
    p.drawLine(strip.topLeft(), strip.bottomLeft());
    for (int y = 0; y < int(overview.size()); y++)
        if (overview[size_t(y)] > 0.0f)
            p.fillRect(QRect(strip.x() + 3, strip.y() + y, strip.width() - 4, 1), heatColor(overview[size_t(y)]));
    if (rowCount() > 0)
    {
        int top = strip.height() * first_row / rowCount();
        int height = std::max(4, strip.height() * visibleRows() / rowCount());
        p.setPen(QPen(highlight, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRect(strip.x() + 2, std::min(top, strip.height() - height), strip.width() - 3, height - 1));
    }
}
