#include "doctest/doctest.h"
#include "TileView.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>

using namespace corrupt;

namespace
{

// 4 bpp tile `k` is solid colour k % 16; from tile 64 on, a diagonal stripe pattern.
MemorySnapshot sample(size_t size = 0x10000)
{
    MemorySnapshot s;
    s.target = Target::VRAM;
    s.bytes.assign(size, 0);
    s.heat.assign(size, 0.0f);
    s.hits.assign(size, 0);
    for (size_t k = 0; k * 32 + 32 <= size && k < 256; k++)
        for (int y = 0; y < 8; y++)
            for (int plane = 0; plane < 4; plane++)
            {
                uint8_t row = k < 64 ? ((k % 16) >> plane & 1 ? 0xFF : 0x00) : uint8_t(0x81 >> ((y + plane) % 8));
                s.bytes[k * 32 + size_t((plane / 2) * 16 + y * 2 + (plane & 1))] = row;
            }
    for (size_t i = 2 * 32; i < 3 * 32; i++) // tile 2 is busy
        s.heat[i] = 40.0f;
    return s;
}

std::vector<uint8_t> ramp_cgram()
{
    std::vector<uint8_t> c(512);
    for (int i = 0; i < 256; i++)
    {
        uint16_t bgr = uint16_t((i % 32) | ((i * 3) % 32) << 5 | ((31 - i % 32) << 10));
        c[size_t(2 * i)] = uint8_t(bgr);
        c[size_t(2 * i + 1)] = uint8_t(bgr >> 8);
    }
    return c;
}

QColor colourOf(Rgb c)
{
    return QColor(c.r, c.g, c.b);
}

void mouse(TileView &v, QEvent::Type type, QPoint at, Qt::KeyboardModifiers mods = Qt::NoModifier,
           Qt::MouseButtons buttons = Qt::LeftButton)
{
    if (type == QEvent::MouseButtonRelease)
        buttons = Qt::NoButton;
    QMouseEvent e(type, QPointF(at), v.viewport()->mapToGlobal(QPointF(at)), Qt::LeftButton, buttons, mods);
    QApplication::sendEvent(v.viewport(), &e);
}

void click(TileView &v, QPoint at, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    mouse(v, QEvent::MouseButtonPress, at, mods);
    mouse(v, QEvent::MouseButtonRelease, at, mods);
}

QColor colourAt(TileView &v, QPoint at)
{
    return v.viewport()->grab().toImage().pixelColor(at);
}

void shot(QWidget &w, const QString &name)
{
    if (auto dir = getenv("CORRUPT_SHOTS_DIR"))
    {
        QDir().mkpath(dir);
        w.grab().save(QString("%1/%2.png").arg(dir, name));
    }
}

struct Shown
{
    TileView view;
    explicit Shown(int zoom = 2)
    {
        view.resize(500, 500);
        view.setZoom(zoom);
        view.setFormat(TileFormat::Bpp4);
        view.setCgram(ramp_cgram());
        view.setSnapshot(sample());
        view.show();
        QApplication::processEvents();
    }
};

} // namespace

TEST_CASE("tiles and points map both ways at every zoom and width")
{
    Shown s;
    for (int zoom : { 1, 2, 4, 8 })
        for (int per_row : { 4, 16, 32 })
        {
            CAPTURE(zoom);
            CAPTURE(per_row);
            s.view.setZoom(zoom);
            s.view.setTilesPerRow(per_row);
            uint32_t n = uint32_t(per_row);
            for (uint32_t tile : { 0u, 1u, n - 1, n, 2 * n + 1 })
            {
                CAPTURE(tile);
                QRect r = s.view.tileRect(tile);
                if (r.isEmpty()) // wider than the view at high zoom: scrolled off sideways
                    continue;
                CHECK(r.width() == 8 * zoom);
                CHECK(r.height() == 8 * zoom);
                CHECK(s.view.tileAt(r.topLeft()) == tile);
                CHECK(s.view.tileAt(r.bottomRight()) == tile);
            }
            CHECK(s.view.tileRect(n).top() == s.view.tileRect(0).top() + 8 * zoom);
        }
    CHECK_FALSE(s.view.tileAt(QPoint(3, 3))); // the address column
}

TEST_CASE("a tile covers its format's bytes")
{
    Shown s;
    CHECK(s.view.tileCount() == 0x10000 / 32);
    s.view.setFormat(TileFormat::Bpp2);
    CHECK(s.view.tileCount() == 0x10000 / 16);
    s.view.setFormat(TileFormat::Mode7);
    CHECK(s.view.tileCount() == 0x10000 / 128);
    s.view.setFormat(TileFormat::Bpp4);
}

TEST_CASE("selection is whole tiles, and survives a format change as bytes")
{
    Shown s;
    click(s.view, s.view.tileRect(3).center());
    CHECK(s.view.selection() == std::pair{ 3u * 32, 3u * 32 + 31 });
    click(s.view, s.view.tileRect(10).center(), Qt::ShiftModifier);
    CHECK(s.view.selection() == std::pair{ 3u * 32, 10u * 32 + 31 });

    mouse(s.view, QEvent::MouseButtonPress, s.view.tileRect(20).center());
    mouse(s.view, QEvent::MouseMove, s.view.tileRect(17).center());
    mouse(s.view, QEvent::MouseButtonRelease, s.view.tileRect(17).center());
    CHECK(s.view.selection() == std::pair{ 17u * 32, 20u * 32 + 31 });

    s.view.select(0x1010, 0x1010); // a byte inside tile 0x80
    CHECK(s.view.selection() == std::pair{ 0x1000u, 0x101Fu });
    s.view.setFormat(TileFormat::Bpp2); // 16-byte tiles: the same byte is in the second half
    CHECK(s.view.selection() == std::pair{ 0x1010u, 0x101Fu });
    CHECK_FALSE(s.view.tileRect(0x1010 / 16).isEmpty());
}

TEST_CASE("scrolling and the place across format and zoom changes")
{
    Shown s;
    s.view.scrollTo(0x4000);
    CHECK(s.view.firstVisibleOffset() <= 0x4000u);
    CHECK(s.view.firstVisibleOffset() + 16 * 32 > 0x4000u);
    s.view.setFormat(TileFormat::Bpp2);
    CHECK(s.view.firstVisibleOffset() <= 0x4000u);
    CHECK(s.view.firstVisibleOffset() + 16 * 16 > 0x4000u);
    s.view.setZoom(4);
    CHECK(s.view.firstVisibleOffset() <= 0x4000u);
}

TEST_CASE("pixels are coloured with the chosen palette row, or grey")
{
    Shown s(4);
    std::vector<uint8_t> cgram = ramp_cgram();
    QPoint five = s.view.tileRect(5).center(); // solid colour 5
    CHECK(colourAt(s.view, five) == colourOf(palette_colour(cgram, 5)));
    s.view.setPaletteRow(1);
    CHECK(s.view.paletteRow() == 1);
    CHECK(colourAt(s.view, five) == colourOf(palette_colour(cgram, 16 + 5)));
    s.view.setPaletteRow(TileView::kGrey);
    CHECK(colourAt(s.view, five) == QColor(85, 85, 85)); // 5 of 15
    s.view.setPaletteRow(99);
    CHECK(s.view.paletteRow() == 15); // 4 bpp has 16 rows

    s.view.setPaletteRow(0);
    auto changed = cgram;
    changed[10] = 0x1F; // colour 5 turns red: the palette is live
    changed[11] = 0x00;
    s.view.setCgram(changed);
    CHECK(colourAt(s.view, five) == QColor(255, 0, 0));
}

TEST_CASE("the activity overlay tints busy tiles only")
{
    Shown s(4);
    s.view.setPaletteRow(TileView::kGrey);
    s.view.setOverlay(MemoryPane::Overlay::None);
    QColor plain2 = colourAt(s.view, s.view.tileRect(2).center());
    QColor plain3 = colourAt(s.view, s.view.tileRect(3).center());
    s.view.setOverlay(MemoryPane::Overlay::Activity);
    CHECK(colourAt(s.view, s.view.tileRect(2).center()) != plain2);
    CHECK(colourAt(s.view, s.view.tileRect(3).center()) == plain3);
}

TEST_CASE("the danger overlay tints dangerous tiles red")
{
    Shown s(4);
    s.view.setPaletteRow(TileView::kGrey);
    auto snap = sample();
    snap.danger.assign(snap.bytes.size(), 0.0f);
    for (size_t i = 7 * 32; i < 7 * 32 + 16; i++)
        snap.danger[i] = 0.8f;
    s.view.setSnapshot(snap);
    s.view.setOverlay(MemoryPane::Overlay::Danger);
    QColor tinted = colourAt(s.view, s.view.tileRect(7).center());
    QColor plain = colourAt(s.view, s.view.tileRect(6).center());
    CHECK(tinted.red() > tinted.green() + 40);
    CHECK(plain.red() == plain.green());
}

TEST_CASE("hover, ctrl+wheel zoom and tiles-per-row limits")
{
    Shown s(2);
    std::optional<uint32_t> hovered;
    s.view.onHover = [&](std::optional<uint32_t> t) { hovered = t; };
    mouse(s.view, QEvent::MouseMove, s.view.tileRect(37).center(), Qt::NoModifier, Qt::NoButton);
    CHECK(hovered == 37u);
    mouse(s.view, QEvent::MouseMove, QPoint(2, 2), Qt::NoModifier, Qt::NoButton);
    CHECK_FALSE(hovered);

    QPoint at = s.view.tileRect(40).center();
    QWheelEvent wheel(QPointF(at), s.view.viewport()->mapToGlobal(QPointF(at)), QPoint(), QPoint(0, 120), Qt::NoButton,
                      Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(s.view.viewport(), &wheel);
    CHECK(s.view.zoom() == 3);
    CHECK_FALSE(s.view.tileRect(40).isEmpty());

    s.view.setTilesPerRow(0);
    CHECK(s.view.tilesPerRow() == 1);
    s.view.setTilesPerRow(500);
    CHECK(s.view.tilesPerRow() == TileView::kMaxTilesPerRow);
    for (int i = 0; i < 10; i++)
        s.view.zoomIn();
    CHECK(s.view.zoom() == TileView::kMaxZoom);
}

TEST_CASE("mode 7 tiles are 128 bytes apart")
{
    Shown s;
    s.view.setFormat(TileFormat::Mode7);
    click(s.view, s.view.tileRect(3).center());
    CHECK(s.view.selection() == std::pair{ 384u, 511u });
}

TEST_CASE("an empty snapshot draws nothing and selects nothing")
{
    TileView v;
    v.resize(300, 300);
    v.setSnapshot({});
    v.show();
    QApplication::processEvents();
    CHECK_FALSE(v.tileAt(QPoint(150, 50)));
    CHECK(v.tileRect(0).isEmpty());
    CHECK(v.tileCount() == 0);
}

TEST_CASE("pictures of the tile view")
{
    Shown s(3);
    s.view.select(0x800, 0x9FF);
    shot(s.view, "tile-view-4bpp");
    s.view.setPaletteRow(TileView::kGrey);
    s.view.setFormat(TileFormat::Bpp2);
    shot(s.view, "tile-view-2bpp-grey");

    auto dir = getenv("CORRUPT_DUMP_DIR");
    if (!dir)
        return;
    QFile vram(QString("%1/vram.bin").arg(dir)), cgram(QString("%1/cgram.bin").arg(dir));
    if (!vram.open(QIODevice::ReadOnly) || !cgram.open(QIODevice::ReadOnly))
        return;
    auto bytes = vram.readAll();
    auto colours = cgram.readAll();
    MemorySnapshot real;
    real.target = Target::VRAM;
    real.bytes.assign(bytes.begin(), bytes.end());
    real.heat.assign(real.bytes.size(), 0.0f);
    real.hits.assign(real.bytes.size(), 0);
    TileView v;
    v.resize(600, 700);
    v.setOverlay(MemoryPane::Overlay::None);
    v.setSnapshot(real);
    v.setCgram(std::vector<uint8_t>(colours.begin(), colours.end()));
    v.show();
    QApplication::processEvents();
    v.setZoom(2);
    v.setTilesPerRow(32);
    // Every 4 bpp area, at a few palettes, for judging by eye.
    for (uint32_t at : { 0x0000u, 0x4000u, 0x6000u, 0x8000u, 0xC000u, 0xE000u })
        for (int row : { 0, 2, 8 })
        {
            v.setFormat(TileFormat::Bpp4);
            v.setPaletteRow(row);
            v.scrollTo(at);
            shot(v, QString("real-tiles-%1-p%2").arg(at, 4, 16, QChar('0')).arg(row));
        }
}
