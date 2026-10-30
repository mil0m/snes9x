#include "doctest/doctest.h"
#include "PixelView.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include <random>

using namespace corrupt;

namespace
{

// Something shaped like a game's WRAM: zero fill, a pointer table, a gradient, sprite
// entries, noise, and a 0xFF fill, with a hot spot and recipe hits.
MemorySnapshot sample(Target t = Target::WRAM, size_t size = 0x20000)
{
    MemorySnapshot s;
    s.target = t;
    s.bytes.assign(size, 0);
    s.heat.assign(size, 0.0f);
    s.hits.assign(size, 0);
    std::mt19937 rng(7);
    for (size_t i = 0; i < size; i++)
    {
        if (i < 0x100)
            s.bytes[i] = uint8_t(rng());              // direct page: busy variables
        else if (i < 0x800)
            s.bytes[i] = i % 2 ? 0x80 : uint8_t(i / 2); // a table of 16-bit pointers
        else if (i >= 0x1000 && i < 0x1400)
            s.bytes[i] = uint8_t(i % 4 == 3 ? 0x30 : i * 5); // 4-byte sprite entries
        else if (i >= 0x2000 && i < 0x6000)
            s.bytes[i] = uint8_t((i - 0x2000) / 64);   // a smooth ramp
        else if (i >= 0x8000 && i < 0xC000)
            s.bytes[i] = uint8_t(rng() % 7 ? rng() : 0); // compressed graphics
        else if (i >= 0x10000 && i < 0x14000)
            s.bytes[i] = 0xFF;
    }
    for (size_t i = 0; i < std::min<size_t>(0x100, size); i++)
        s.heat[i] = float(i % 50);
    for (size_t i = 0x1000; i < std::min<size_t>(0x1400, size); i += 3)
        s.hits[i] = uint32_t(1 + i % 5);
    return s;
}

void mouse(PixelView &v, QEvent::Type type, QPoint at, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    Qt::MouseButtons buttons = type == QEvent::MouseButtonRelease || type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent e(type, QPointF(at), v.viewport()->mapToGlobal(QPointF(at)), Qt::LeftButton, buttons, mods);
    QApplication::sendEvent(v.viewport(), &e);
}

void click(PixelView &v, QPoint at, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    mouse(v, QEvent::MouseButtonPress, at, mods);
    mouse(v, QEvent::MouseButtonRelease, at, mods);
}

void wheel(PixelView &v, QPoint at, int delta, Qt::KeyboardModifiers mods)
{
    QWheelEvent e(QPointF(at), v.viewport()->mapToGlobal(QPointF(at)), QPoint(), QPoint(0, delta), Qt::NoButton, mods,
                  Qt::NoScrollPhase, false);
    QApplication::sendEvent(v.viewport(), &e);
}

void shot(PixelView &v, const char *name)
{
    if (auto dir = getenv("CORRUPT_SHOTS_DIR"))
    {
        QDir().mkpath(dir);
        v.grab().save(QString("%1/%2.png").arg(dir, name));
    }
}

struct Shown
{
    PixelView view;
    explicit Shown(int zoom = 2, QSize size = QSize(700, 500))
    {
        view.resize(size);
        view.setZoom(zoom);
        view.setSnapshot(sample());
        view.show();
        QApplication::processEvents();
    }
};

bool power_of_two(int n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

} // namespace

TEST_CASE("a row holds as many bytes as fit, a power of two, and zooming in halves it")
{
    Shown s(1);
    int at1 = s.view.columns();
    CHECK(power_of_two(at1));
    CHECK(at1 >= 256);
    CHECK(at1 <= s.view.viewport()->width());
    s.view.setZoom(2);
    CHECK(s.view.columns() == at1 / 2);
    s.view.setZoom(16);
    CHECK(power_of_two(s.view.columns()));
    CHECK(s.view.columns() * 16 <= s.view.viewport()->width());
}

TEST_CASE("a narrow view still shows 16 bytes a row, and a small target is no wider than square")
{
    Shown s(16, QSize(200, 300));
    CHECK(s.view.columns() == 16);
    s.view.setZoom(1);
    s.view.setSnapshot(sample(Target::CGRAM, 512));
    CHECK(s.view.columns() == 32); // one palette a row
    s.view.setSnapshot(sample(Target::OAM, 544));
    CHECK(s.view.columns() == 32);
    s.view.setSnapshot(sample(Target::VRAM, 0x10000));
    CHECK(s.view.columns() < 256); // the width decides, not the square
}

TEST_CASE("bytes and pixels map both ways, at every zoom")
{
    Shown s;
    for (int zoom : { 1, 2, 3, 4, 6, 8, 12, 16 })
    {
        CAPTURE(zoom);
        s.view.setZoom(zoom);
        REQUIRE(s.view.zoom() == zoom);
        uint32_t cols = uint32_t(s.view.columns());
        for (uint32_t offset : { 0u, 1u, cols - 1, cols, 5 * cols + 3 })
        {
            CAPTURE(offset);
            auto rect = s.view.pixelRect(offset);
            REQUIRE_FALSE(rect.isEmpty());
            CHECK(rect.width() == zoom);
            CHECK(rect.height() == zoom);
            CHECK(s.view.offsetAt(rect.topLeft()) == offset);
            CHECK(s.view.offsetAt(rect.bottomRight()) == offset);
        }
        CHECK(s.view.pixelRect(1).left() == s.view.pixelRect(0).left() + zoom);
        CHECK(s.view.pixelRect(cols).top() == s.view.pixelRect(0).top() + zoom);
    }
    CHECK_FALSE(s.view.offsetAt(QPoint(2, 2))); // the address column
}

TEST_CASE("scrolling to an offset shows its row first, and zooming keeps the place")
{
    Shown s(4);
    s.view.scrollTo(0x1234);
    uint32_t cols = uint32_t(s.view.columns());
    CHECK(s.view.firstVisibleOffset() == 0x1234u / cols * cols);
    CHECK_FALSE(s.view.pixelRect(0x1234).isEmpty());
    s.view.setZoom(8);
    CHECK(s.view.firstVisibleOffset() <= 0x1234u);
    CHECK(s.view.firstVisibleOffset() + uint32_t(s.view.columns()) > 0x1234u);
    s.view.setZoom(1);
    CHECK(s.view.firstVisibleOffset() <= 0x1234u);
}

TEST_CASE("dragging selects a range in either direction; shift-click extends")
{
    Shown s(8);
    uint32_t cols = uint32_t(s.view.columns());
    mouse(s.view, QEvent::MouseButtonPress, s.view.pixelRect(3 * cols + 5).center());
    {
        QMouseEvent e(QEvent::MouseMove, QPointF(s.view.pixelRect(cols + 2).center()), QPointF(), Qt::NoButton,
                      Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(s.view.viewport(), &e);
    }
    mouse(s.view, QEvent::MouseButtonRelease, s.view.pixelRect(cols + 2).center());
    auto sel = s.view.selection();
    REQUIRE(sel);
    CHECK(sel->first == cols + 2);
    CHECK(sel->second == 3 * cols + 5);

    int changes = 0;
    s.view.onSelectionChanged = [&] { changes++; };
    click(s.view, s.view.pixelRect(0x30).center());
    CHECK(s.view.selection() == std::pair{ 0x30u, 0x30u });
    click(s.view, s.view.pixelRect(0x52).center(), Qt::ShiftModifier);
    CHECK(s.view.selection() == std::pair{ 0x30u, 0x52u });
    CHECK(changes >= 2);
}

TEST_CASE("select() scrolls the range into view; a new target clears it")
{
    Shown s(8);
    s.view.select(0x8000, 0x80FF);
    CHECK(s.view.selection() == std::pair{ 0x8000u, 0x80FFu });
    CHECK_FALSE(s.view.pixelRect(0x8000).isEmpty());
    s.view.setSnapshot(sample()); // same target: kept
    CHECK(s.view.selection());
    s.view.setSnapshot(sample(Target::VRAM, 0x10000));
    CHECK_FALSE(s.view.selection());
    CHECK(s.view.firstVisibleOffset() == 0u);
}

TEST_CASE("ctrl+wheel zooms around the mouse; the plain wheel scrolls")
{
    Shown s(2);
    s.view.scrollTo(0x4000);
    QPoint at = s.view.pixelRect(0x4000 + 40 * uint32_t(s.view.columns()) + 7).center();
    uint32_t under = *s.view.offsetAt(at);
    wheel(s.view, at, 120, Qt::ControlModifier);
    CHECK(s.view.zoom() == 3);
    QRect now = s.view.pixelRect(under);
    REQUIRE_FALSE(now.isEmpty());
    CHECK(std::abs(now.center().y() - at.y()) <= 3);

    wheel(s.view, at, -120, Qt::ControlModifier);
    CHECK(s.view.zoom() == 2);

    uint32_t before = s.view.firstVisibleOffset();
    wheel(s.view, at, -120, Qt::NoModifier);
    CHECK(s.view.zoom() == 2);
    CHECK(s.view.firstVisibleOffset() > before);
}

TEST_CASE("zoom steps stop at 1 and 16")
{
    Shown s(1);
    s.view.zoomOut();
    CHECK(s.view.zoom() == 1);
    for (int i = 0; i < 20; i++)
        s.view.zoomIn();
    CHECK(s.view.zoom() == 16);
    int told = 0;
    s.view.onZoomChanged = [&] { told++; };
    s.view.zoomOut();
    CHECK(s.view.zoom() == 12);
    CHECK(told == 1);
}

TEST_CASE("hovering tells which byte is under the mouse")
{
    Shown s(8);
    std::optional<uint32_t> hovered;
    s.view.onHover = [&](std::optional<uint32_t> o) { hovered = o; };
    mouse(s.view, QEvent::MouseMove, s.view.pixelRect(0x123).center());
    CHECK(hovered == 0x123u);
    mouse(s.view, QEvent::MouseMove, QPoint(1, 1));
    CHECK_FALSE(hovered);
}

TEST_CASE("an empty snapshot draws nothing and selects nothing")
{
    PixelView v;
    v.resize(400, 300);
    v.setSnapshot({});
    v.show();
    QApplication::processEvents();
    CHECK_FALSE(v.offsetAt(QPoint(200, 100)));
    CHECK(v.pixelRect(0).isEmpty());
    shot(v, "pixel-view-empty");
}

TEST_SUITE("a chosen row width")
{
    TEST_CASE("a chosen width holds at every zoom and window size; 0 goes back to auto")
    {
        Shown s(2);
        int automatic = s.view.columns();
        s.view.setRowWidth(48);
        CHECK(s.view.rowWidth() == 48);
        CHECK(s.view.columns() == 48);
        s.view.setZoom(1);
        CHECK(s.view.columns() == 48);
        s.view.resize(300, 300);
        QApplication::processEvents();
        CHECK(s.view.columns() == 48);
        s.view.resize(700, 500);
        QApplication::processEvents();
        s.view.setZoom(2);
        s.view.setRowWidth(0);
        CHECK(s.view.rowWidth() == 0);
        CHECK(s.view.columns() == automatic);
    }

    TEST_CASE("bytes and pixels map both ways at odd widths")
    {
        Shown s(3);
        for (int width : { 1, 7, 48, 100 })
        {
            CAPTURE(width);
            s.view.setRowWidth(width);
            uint32_t w = uint32_t(width);
            for (uint32_t offset : { 0u, w - 1, w, 3 * w + w / 2 })
            {
                CAPTURE(offset);
                auto rect = s.view.pixelRect(offset);
                REQUIRE_FALSE(rect.isEmpty());
                CHECK(s.view.offsetAt(rect.topLeft()) == offset);
                CHECK(s.view.offsetAt(rect.bottomRight()) == offset);
            }
            CHECK(s.view.pixelRect(w).top() == s.view.pixelRect(0).top() + 3);
            CHECK(s.view.pixelRect(w).left() == s.view.pixelRect(0).left());
        }
    }

    TEST_CASE("changing the width keeps the place")
    {
        Shown s(4);
        s.view.scrollTo(0x6000);
        s.view.setRowWidth(48);
        CHECK(s.view.firstVisibleOffset() <= 0x6000u);
        CHECK(s.view.firstVisibleOffset() + 48 > 0x6000u);
    }

    TEST_CASE("rows wider than the view scroll sideways, under a fixed address column")
    {
        Shown s(4);
        s.view.setRowWidth(300); // 1200 pixels in a 700-pixel view
        auto h = s.view.horizontalScrollBar();
        CHECK(h->maximum() > 0);
        CHECK_FALSE(s.view.pixelRect(0).isEmpty());
        CHECK(s.view.pixelRect(299).isEmpty()); // off to the right
        h->setValue(h->maximum());
        QRect last = s.view.pixelRect(299);
        REQUIRE_FALSE(last.isEmpty());
        CHECK(last.right() < s.view.viewport()->width());
        CHECK(s.view.offsetAt(last.center()) == 299u);
        CHECK(s.view.pixelRect(0).isEmpty()); // scrolled under the address column
        CHECK_FALSE(s.view.offsetAt(QPoint(5, 5)));

        click(s.view, last.center());
        CHECK(s.view.selection() == std::pair{ 299u, 299u });
        s.view.select(0x9000, 0x9000); // select() brings it into view sideways too
        CHECK_FALSE(s.view.pixelRect(0x9000).isEmpty());
        shot(s.view, "pixel-view-wide-rows");

        s.view.setRowWidth(0);
        CHECK(h->maximum() == 0);
    }

    TEST_CASE("the width is clamped to 1..4096")
    {
        Shown s(1);
        s.view.setRowWidth(100000);
        CHECK(s.view.rowWidth() == PixelView::kMaxRowWidth);
        CHECK(s.view.columns() == 4096);
        s.view.setRowWidth(-3);
        CHECK(s.view.rowWidth() == 0);
    }

    TEST_CASE("a picture: the sample's 4-byte sprite entries line up at width 32")
    {
        Shown s(8);
        s.view.setOverlay(MemoryPane::Overlay::None);
        s.view.setRowWidth(34); // off by two: the entries lean
        s.view.scrollTo(0x1000);
        shot(s.view, "pixel-view-width-34");
        s.view.setRowWidth(32);
        s.view.scrollTo(0x1000);
        shot(s.view, "pixel-view-width-32");
    }
}

TEST_CASE("the danger overlay paints the crash map in red")
{
    Shown s(8);
    auto snap = sample();
    snap.danger.assign(snap.bytes.size(), 0.0f);
    for (size_t i = 0x20; i < 0x30; i++)
        snap.danger[i] = 0.6f;
    s.view.setSnapshot(snap);
    s.view.setOverlay(MemoryPane::Overlay::Danger);
    QImage img = s.view.viewport()->grab().toImage();
    QColor hot = img.pixelColor(s.view.pixelRect(0x25).center());
    QColor cool = img.pixelColor(s.view.pixelRect(0x45).center());
    CHECK(hot.red() > hot.green() + 60);
    CHECK(hot.red() > hot.blue() + 60);
    CHECK(cool.red() == cool.green()); // grey: no danger there
    shot(s.view, "pixel-view-danger");
}

TEST_CASE("pictures of the pixel view")
{
    Shown s(1, QSize(600, 560));
    s.view.setOverlay(MemoryPane::Overlay::None);
    s.view.select(0x1000, 0x13FF);
    shot(s.view, "pixel-view-plain-1x");
    s.view.setOverlay(MemoryPane::Overlay::Activity);
    shot(s.view, "pixel-view-activity-1x");
    s.view.setZoom(8);
    s.view.setOverlay(MemoryPane::Overlay::Hits);
    s.view.scrollTo(0xF00);
    shot(s.view, "pixel-view-hits-8x");
    s.view.setOverlay(MemoryPane::Overlay::None);
    s.view.clearSelection();
    s.view.scrollTo(0);
    shot(s.view, "pixel-view-plain-8x");
}

TEST_CASE("pictures of real memory, from corrupt-smoke --dump")
{
    auto dir = getenv("CORRUPT_DUMP_DIR");
    auto shots = getenv("CORRUPT_SHOTS_DIR");
    if (!dir || !shots)
        return; // only when asked: corrupt-smoke <rom> <recipe> 0 1800 --dump <dir>
    for (auto t : { Target::WRAM, Target::VRAM, Target::ARAM, Target::CGRAM })
    {
        QFile f(QString("%1/%2.bin").arg(dir, target_name(t)));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        auto data = f.readAll();
        MemorySnapshot s;
        s.target = t;
        s.bytes.assign(data.begin(), data.end());
        s.heat.assign(s.bytes.size(), 0.0f);
        s.hits.assign(s.bytes.size(), 0);
        PixelView v;
        v.resize(600, 600);
        v.setOverlay(MemoryPane::Overlay::None);
        v.setSnapshot(s);
        v.show();
        QApplication::processEvents();
        for (int zoom : { 1, 4 })
        {
            v.setZoom(zoom);
            QDir().mkpath(shots);
            v.grab().save(QString("%1/real-%2-%3x.png").arg(shots, target_name(t)).arg(zoom));
        }
        if (t == Target::VRAM) // a 32-wide tile map is 64 bytes a row: it should line up at 64, not 66
            for (int width : { 64, 66 })
            {
                v.setZoom(4);
                v.setRowWidth(width);
                v.scrollTo(0);
                v.grab().save(QString("%1/real-vram-row%2.png").arg(shots).arg(width));
            }
    }
}
