#include "doctest/doctest.h"
#include "MemoryView.hpp"

#include <QApplication>
#include <QDir>
#include <QMouseEvent>
#include <QScrollBar>

using namespace corrupt;

namespace
{

MemorySnapshot sample(Target t = Target::WRAM, size_t size = 0x20000)
{
    MemorySnapshot s;
    s.target = t;
    s.bytes.resize(size);
    s.heat.assign(size, 0.0f);
    s.hits.assign(size, 0);
    for (size_t i = 0; i < size; i++)
        s.bytes[i] = uint8_t(i * 7);
    for (size_t i = 0x10; i < std::min<size_t>(0x30, size); i++) // a hot spot, like a frame counter area
        s.heat[i] = float(i - 0x10) * 1.5f;
    for (size_t i = 0x1000; i < std::min<size_t>(0x1400, size); i += 3) // where a recipe has been writing
        s.hits[i] = uint32_t(1 + i % 5);
    return s;
}

void mouse(MemoryView &v, QEvent::Type type, QPoint at, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    Qt::MouseButtons buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent e(type, QPointF(at), v.viewport()->mapToGlobal(QPointF(at)), Qt::LeftButton, buttons, mods);
    QApplication::sendEvent(v.viewport(), &e);
}

void click(MemoryView &v, QPoint at, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    mouse(v, QEvent::MouseButtonPress, at, mods);
    mouse(v, QEvent::MouseButtonRelease, at, mods);
}

void shot(MemoryView &v, const char *name)
{
    if (auto dir = getenv("CORRUPT_SHOTS_DIR"))
    {
        QDir().mkpath(dir);
        v.grab().save(QString("%1/%2.png").arg(dir, name));
    }
}

struct Shown
{
    MemoryView view;
    Shown()
    {
        view.resize(view.minimumSizeHint().width(), 400);
        view.setSnapshot(sample());
        view.show();
        QApplication::processEvents();
    }
};

} // namespace

TEST_CASE("the minimum size fits the whole grid beside the overview strip")
{
    Shown s;
    CHECK(s.view.cellRect(0xF).right() < s.view.overviewRect().left());
}

TEST_CASE("cells and offsets map both ways")
{
    Shown s;
    for (uint32_t offset : { 0u, 1u, 0xFu, 0x10u, 0x35u })
    {
        auto rect = s.view.cellRect(offset);
        REQUIRE_FALSE(rect.isEmpty());
        CHECK(s.view.offsetAt(rect.center()) == offset);
    }
    CHECK_FALSE(s.view.offsetAt(QPoint(2, 2))); // the address column
}

TEST_CASE("dragging selects a range, in either direction")
{
    Shown s;
    mouse(s.view, QEvent::MouseButtonPress, s.view.cellRect(0x45).center());
    mouse(s.view, QEvent::MouseMove, s.view.cellRect(0x23).center());
    mouse(s.view, QEvent::MouseButtonRelease, s.view.cellRect(0x23).center());
    auto sel = s.view.selection();
    REQUIRE(sel);
    CHECK(sel->first == 0x23u);
    CHECK(sel->second == 0x45u);
    shot(s.view, "memory-view-selection");
}

TEST_CASE("a click selects one byte; shift-click extends")
{
    Shown s;
    int changes = 0;
    s.view.onSelectionChanged = [&] { changes++; };
    click(s.view, s.view.cellRect(0x30).center());
    REQUIRE(s.view.selection());
    CHECK(s.view.selection()->first == 0x30u);
    CHECK(s.view.selection()->second == 0x30u);
    click(s.view, s.view.cellRect(0x52).center(), Qt::ShiftModifier);
    CHECK(s.view.selection()->first == 0x30u);
    CHECK(s.view.selection()->second == 0x52u);
    CHECK(changes >= 2);
}

TEST_CASE("scrolling to an offset shows its row first")
{
    Shown s;
    s.view.scrollTo(0x1234);
    CHECK(s.view.firstVisibleOffset() == 0x1230u);
    s.view.select(0x8000, 0x80FF);
    CHECK(s.view.selection()->first == 0x8000u);
    CHECK(s.view.firstVisibleOffset() <= 0x8000u);
    CHECK_FALSE(s.view.cellRect(0x8000).isEmpty()); // on screen
    s.view.setOverlay(MemoryView::Overlay::Hits);
    s.view.scrollTo(0x1000);
    shot(s.view, "memory-view-hits");
}

TEST_CASE("clicking the overview strip jumps there")
{
    Shown s;
    QRect strip = s.view.overviewRect();
    REQUIRE_FALSE(strip.isEmpty());
    click(s.view, QPoint(strip.center().x(), strip.bottom() - 1));
    CHECK(s.view.firstVisibleOffset() > 0x18000u);
    CHECK_FALSE(s.view.selection()); // the strip scrolls, it doesn't select
    shot(s.view, "memory-view-bottom");
}

TEST_CASE("a new snapshot keeps the selection and scroll; another target clears them")
{
    Shown s;
    s.view.select(0x100, 0x10F);
    s.view.scrollTo(0x2000);
    s.view.setSnapshot(sample());
    CHECK(s.view.selection());
    CHECK(s.view.firstVisibleOffset() == 0x2000u);
    s.view.setSnapshot(sample(Target::CGRAM, 512));
    CHECK_FALSE(s.view.selection());
    CHECK(s.view.firstVisibleOffset() == 0u);
    CHECK(s.view.verticalScrollBar()->maximum() >= 0);
    shot(s.view, "memory-view-cgram");
}

TEST_CASE("an empty snapshot draws nothing and selects nothing")
{
    MemoryView v;
    v.resize(400, 300);
    v.setSnapshot({});
    v.show();
    QApplication::processEvents();
    CHECK_FALSE(v.offsetAt(QPoint(200, 100)));
    CHECK(v.cellRect(0).isEmpty());
}
