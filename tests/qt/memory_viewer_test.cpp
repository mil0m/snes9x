#include "doctest/doctest.h"
#include "MemoryViewer.hpp"
#include "MemoryView.hpp"
#include "PixelView.hpp"
#include "TileView.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QScrollBar>
#include <QPushButton>
#include <QSpinBox>

using namespace corrupt;

namespace
{

struct FakeSource : MemorySource
{
    std::vector<Target> watched;
    int stops = 0;
    int clears = 0;
    std::vector<Target> copies_asked;

    void watch(Target t) override { watched.push_back(t); }
    void stop() override { stops++; }
    void clearHits() override { clears++; }
    MemorySnapshot snapshot() override
    {
        MemorySnapshot s;
        s.target = watched.empty() ? Target::WRAM : watched.back();
        size_t size = nominal_size(s.target);
        s.bytes.assign(size, 0x11);
        for (size_t i = 0; i < size; i += 4) // some structure for the pictures
            s.bytes[i] = uint8_t(i >> 6);
        if (s.target != Target::ROM) // like MemoryWatch: no heat for the ROM, hits only once there are some
        {
            s.heat.assign(size, 0.0f);
            s.hits.assign(size, 0);
        }
        for (size_t i = 0x1C00; i < std::min<size_t>(0x1E20, s.heat.size()); i++)
            s.heat[i] = 20.0f;
        s.crash_blocks.resize((size + 15) / 16);
        s.hot_blocks.assign(s.crash_blocks.size(), 0);
        if (s.target == Target::WRAM)
        {
            s.reads.assign(size, 0.0f);
            s.writes.assign(size, 0.0f);
            s.reads[0x2000] = 3.0f;
            s.writes[0x2000] = 0.5f;
            s.reads[0x2001] = 1234.0f;
        }
        if (s.crash_blocks.size() > 0x1F3)
        {
            s.crash_blocks[0x1F3] = { 5, 12 }; // 0x1F30
            s.hot_blocks[0x1F3] = 1;
        }
        return s;
    }
    std::vector<CopyRun> findCopies(Target of) override
    {
        copies_asked.push_back(of);
        if (of == Target::OAM)
            return { { 0x1C00, 0, 544 } };
        return { { 0x100, 0, 512 }, { 0x7B45, 0, 64 } };
    }
    std::array<size_t, kTargetCount> sizes() override
    {
        std::array<size_t, kTargetCount> out{};
        for (auto t : kAllTargets)
            out[size_t(t)] = nominal_size(t);
        return out;
    }
    std::vector<MapArea> vram_live{ { "BG1 tiles (4 bpp)", { Target::VRAM, 0x0000, 0x3FFF }, "Up to the next area", true },
                                    { "BG1 tile map (32x32)", { Target::VRAM, 0x8000, 0x87FF }, "Which tile goes where", true } };
    std::vector<MapArea> memoryMap(Target t) override
    {
        return t == Target::VRAM ? vram_live : fixed_map(t, nominal_size(t));
    }
    int palette_reads = 0;
    std::vector<uint8_t> cgram = [] {
        std::vector<uint8_t> c(512);
        for (int i = 0; i < 256; i++) // a ramp of colours
        {
            uint16_t bgr = uint16_t((i % 32) | ((i * 3) % 32) << 5 | ((31 - i % 32) << 10));
            c[size_t(2 * i)] = uint8_t(bgr);
            c[size_t(2 * i + 1)] = uint8_t(bgr >> 8);
        }
        return c;
    }();
    std::vector<uint8_t> palette() override
    {
        palette_reads++;
        return cgram;
    }
    Regions named;
    std::vector<std::string> users_of_stats{ "boss rush" };
    Regions regions() override { return named; }
    std::string addRule(const Rule &) override { return {}; }
    std::string saveRegion(const std::string &name, const AddressRange &range) override
    {
        if (auto err = validate_region_name(name))
            return *err;
        named[name] = range;
        return {};
    }
    std::string renameRegion(const std::string &from, const std::string &to) override
    {
        if (!named.count(from) || named.count(to))
            return "no";
        named[to] = named[from];
        named.erase(from);
        return {};
    }
    std::string deleteRegion(const std::string &name) override
    {
        return named.erase(name) ? std::string() : std::string("no");
    }
    std::vector<std::string> regionUsers(const std::string &name) override
    {
        return name == "stats" ? users_of_stats : std::vector<std::string>{};
    }
};

template <typename W>
W *child(QWidget &w, const char *name)
{
    auto c = w.findChild<W *>(name);
    REQUIRE_MESSAGE(c, name);
    return c;
}

void choose(QWidget &w, const char *combo, const QString &data)
{
    auto c = child<QComboBox>(w, combo);
    int i = c->findData(data);
    REQUIRE(i >= 0);
    c->setCurrentIndex(i);
}

} // namespace

TEST_CASE("the viewer watches while it's open, and follows the target")
{
    FakeSource src;
    {
        MemoryViewer v(src);
        v.show();
        QApplication::processEvents();
        REQUIRE_FALSE(src.watched.empty());
        CHECK(src.watched.back() == Target::WRAM);
        choose(v, "target", "cgram");
        CHECK(src.watched.back() == Target::CGRAM);
        v.refresh();
        CHECK(child<MemoryView>(v, "view")->snapshot().bytes.size() == 512);
        v.hide();
        CHECK(src.stops >= 1);
    }
}

TEST_CASE("go to an address, and the selection becomes a ready-made rule")
{
    FakeSource src;
    MemoryViewer v(src);
    v.show();
    v.refresh();
    auto view = child<MemoryView>(v, "view");
    child<QLineEdit>(v, "goto")->setText("$1C00");
    emit child<QLineEdit>(v, "goto")->returnPressed();
    CHECK(view->firstVisibleOffset() == 0x1C00u);

    CHECK_FALSE(child<QPushButton>(v, "new_rule")->isEnabled());
    view->select(0x1C00, 0x1E1F);
    CHECK(child<QPushButton>(v, "new_rule")->isEnabled());
    CHECK(child<QLabel>(v, "selection")->text().contains("0x1C00"));
    Rule r = v.ruleFromSelection();
    CHECK(r.range.target == Target::WRAM);
    CHECK(r.range.first == 0x1C00u);
    CHECK(r.range.last == 0x1E1Fu);
    CHECK_FALSE(r.name.empty());
}

TEST_CASE("found copies are listed, and a click selects the copy in WRAM")
{
    FakeSource src;
    MemoryViewer v(src);
    v.show();
    choose(v, "target", "vram");
    v.refresh();
    child<QPushButton>(v, "find_copies")->click();
    CHECK(src.copies_asked.size() == 2);
    auto list = child<QListWidget>(v, "copies");
    REQUIRE(list->count() == 3);
    CHECK(list->item(0)->text().contains("oam"));
    CHECK(list->item(0)->text().contains("0x1C00"));

    list->setCurrentRow(1);
    emit list->itemActivated(list->item(1));
    CHECK(src.watched.back() == Target::WRAM);
    v.refresh();
    auto sel = child<MemoryView>(v, "view")->selection();
    REQUIRE(sel);
    CHECK(sel->first == 0x100u);
    CHECK(sel->second == 0x2FFu);

    if (auto dir = getenv("CORRUPT_SHOTS_DIR"))
    {
        QDir().mkpath(dir);
        v.grab().save(QString("%1/memory-viewer.png").arg(dir));
    }
}

TEST_CASE("the ROM is a target like any other, in every view")
{
    FakeSource src;
    MemoryViewer v(src);
    v.resize(900, 600);
    v.show();
    choose(v, "target", "rom");
    v.refresh();
    CHECK(src.watched.back() == Target::ROM);
    for (auto mode : { "hex", "pixels", "tiles" })
    {
        choose(v, "mode", mode);
        v.refresh();
        QApplication::processEvents();
    }
    auto pixels = child<PixelView>(v, "pixels");
    CHECK(pixels->snapshot().bytes.size() == nominal_size(Target::ROM));
    CHECK(pixels->snapshot().heat.empty()); // the fake gives none for the ROM either
    choose(v, "mode", "pixels");
    pixels->onHover(0x12345);
    CHECK(child<QLabel>(v, "hover")->text().contains("0x12345"));
}

TEST_CASE("overlay choice and clearing hits")
{
    FakeSource src;
    MemoryViewer v(src);
    v.show();
    choose(v, "overlay", "hits");
    child<QPushButton>(v, "clear_hits")->click();
    choose(v, "overlay", "danger"); // the crash map
    CHECK(child<QComboBox>(v, "overlay")->currentText().contains("crash"));
    choose(v, "overlay", "crashes"); // its frequency map
    CHECK(child<QComboBox>(v, "overlay")->currentText().contains("often"));
    choose(v, "overlay", "reads"); // the game's own accesses
    CHECK(child<QComboBox>(v, "overlay")->currentText().contains("WRAM"));
    choose(v, "overlay", "writes");
    CHECK(child<QComboBox>(v, "overlay")->currentText().contains("WRAM"));
    CHECK(src.clears == 1);
}

TEST_SUITE("named regions in the viewer")
{
    TEST_CASE("a selection can be named, and shows up in the list")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        v.refresh();
        CHECK_FALSE(child<QPushButton>(v, "name_selection")->isEnabled());
        child<MemoryView>(v, "view")->select(0x0F00, 0x0F3F);
        CHECK(child<QPushButton>(v, "name_selection")->isEnabled());
        CHECK(v.nameSelection("stats").isEmpty());
        REQUIRE(src.named.count("stats"));
        CHECK(src.named["stats"].first == 0x0F00u);
        CHECK(src.named["stats"].last == 0x0F3Fu);
        auto list = child<QListWidget>(v, "regions");
        REQUIRE(list->count() == 1);
        CHECK(list->item(0)->text().contains("stats"));
        CHECK(list->item(0)->text().contains("0x0F00"));
    }

    TEST_CASE("a bad name or a taken one is refused with a reason")
    {
        FakeSource src;
        src.named["stats"] = { Target::WRAM, 0, 1 };
        MemoryViewer v(src);
        v.show();
        v.refresh();
        child<MemoryView>(v, "view")->select(0x10, 0x1F);
        CHECK_FALSE(v.nameSelection("1st").isEmpty());
        CHECK_FALSE(v.nameSelection("stats").isEmpty()); // taken: the dialog asks before overwriting
        CHECK(v.nameSelection("stats", true).isEmpty());
        CHECK(src.named["stats"].first == 0x10u);
    }

    TEST_CASE("clicking a region jumps to it and selects it, in its own memory")
    {
        FakeSource src;
        src.named["palette copy"] = { Target::WRAM, 0x0100, 0x02FF };
        src.named["sprite 0"] = { Target::OAM, 0x0000, 0x0003 };
        MemoryViewer v(src);
        v.show();
        choose(v, "target", "vram");
        v.refresh();
        auto list = child<QListWidget>(v, "regions");
        REQUIRE(list->count() == 2);
        emit list->itemActivated(list->item(1)); // sorted by name: "sprite 0"
        CHECK(src.watched.back() == Target::OAM);
        v.refresh();
        auto sel = child<MemoryView>(v, "view")->selection();
        REQUIRE(sel);
        CHECK(sel->first == 0u);
        CHECK(sel->second == 3u);
    }

    TEST_CASE("rename and delete go through the source and refresh the list")
    {
        FakeSource src;
        src.named["stats"] = { Target::WRAM, 0x0F00, 0x0F3F };
        MemoryViewer v(src);
        v.show();
        auto list = child<QListWidget>(v, "regions");
        CHECK(v.renameRegion("stats", "player stats").isEmpty());
        REQUIRE(list->count() == 1);
        CHECK(list->item(0)->text().contains("player stats"));
        CHECK(v.deleteRegion("player stats").isEmpty());
        CHECK(list->count() == 0);
        CHECK(v.usersText("stats").contains("boss rush"));
    }
}

TEST_SUITE("the memory map")
{
    TEST_CASE("the map lists the target's areas, and a click selects one")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        v.refresh();
        auto map = child<QListWidget>(v, "map");
        REQUIRE(map->count() == 4); // WRAM: direct page, low RAM, upper bank $7E, bank $7F
        CHECK(map->item(0)->text().contains("Direct page"));
        CHECK_FALSE(map->item(0)->toolTip().isEmpty());
        emit map->itemActivated(map->item(3));
        auto sel = child<MemoryView>(v, "view")->selection();
        REQUIRE(sel);
        CHECK(sel->first == 0x10000u);
        CHECK(sel->second == 0x1FFFFu);
    }

    TEST_CASE("switching memory switches the map, and live areas follow the game")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        choose(v, "target", "vram");
        v.refresh();
        auto map = child<QListWidget>(v, "map");
        REQUIRE(map->count() == 2);
        CHECK(map->item(1)->text().contains("0x8000"));
        map->setCurrentRow(1);

        src.vram_live[1].range = { Target::VRAM, 0x9000, 0x97FF }; // the game moved its map
        v.refresh();
        REQUIRE(map->count() == 2);
        CHECK(map->item(1)->text().contains("0x9000"));
        CHECK(map->currentRow() == 1); // the same area stays picked

        if (auto dir = getenv("CORRUPT_SHOTS_DIR"))
        {
            emit map->itemActivated(map->item(0));
            v.refresh();
            QDir().mkpath(dir);
            v.grab().save(QString("%1/memory-viewer-map.png").arg(dir));
        }
    }
}


TEST_SUITE("the pixel view in the viewer")
{
    TEST_CASE("switching to pixels and back keeps the selection and the place")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        v.refresh();
        auto hex = child<MemoryView>(v, "view");
        auto pixels = child<PixelView>(v, "pixels");
        CHECK(hex->isVisible());
        CHECK_FALSE(pixels->isVisible());
        CHECK_FALSE(child<QPushButton>(v, "zoom_in")->isVisible());

        hex->scrollTo(0x4000);
        hex->select(0x4010, 0x404F);
        choose(v, "mode", "pixels");
        CHECK(pixels->isVisible());
        CHECK_FALSE(hex->isVisible());
        CHECK(child<QPushButton>(v, "zoom_in")->isVisible());
        CHECK(pixels->snapshot().bytes.size() == 0x20000);
        CHECK(pixels->selection() == std::pair{ 0x4010u, 0x404Fu });
        CHECK(pixels->firstVisibleOffset() <= 0x4000u);
        CHECK_FALSE(pixels->pixelRect(0x4010).isEmpty());
        CHECK(child<QLabel>(v, "selection")->text().contains("0x4010"));

        pixels->select(0x8000, 0x80FF);
        choose(v, "mode", "hex");
        CHECK(hex->selection() == std::pair{ 0x8000u, 0x80FFu });
        CHECK_FALSE(hex->cellRect(0x8000).isEmpty());
    }

    TEST_CASE("a rectangle drawn in the pixel view makes a rectangle rule")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        v.refresh();
        choose(v, "mode", "pixels");
        auto pixels = child<PixelView>(v, "pixels");
        pixels->setRowWidth(64);
        pixels->selectRect(0x1000, 16, 8);
        Rule r = v.ruleFromSelection();
        REQUIRE(r.rect);
        CHECK(r.range.first == 0x1000u);
        CHECK(r.rect->row == 64);
        CHECK(r.rect->width == 16);
        CHECK(r.rect->height == 8);
        CHECK(r.range.last == 0x1000u + 7 * 64 + 15);
        CHECK(r.name == "wram 16x8 at 0x1000");
        CHECK(child<QLabel>(v, "selection")->text().contains("16")); // says it's a rectangle
        CHECK(child<QLabel>(v, "selection")->text().contains("rows of 64"));
    }

    TEST_CASE("in pixel mode the map, copies, go to and new rule use the pixel view")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        v.refresh();
        choose(v, "mode", "pixels");
        auto pixels = child<PixelView>(v, "pixels");

        auto map = child<QListWidget>(v, "map");
        emit map->itemActivated(map->item(3));
        CHECK(pixels->selection() == std::pair{ 0x10000u, 0x1FFFFu });
        Rule r = v.ruleFromSelection();
        CHECK(r.range.first == 0x10000u);
        CHECK(r.range.last == 0x1FFFFu);
        CHECK(child<QPushButton>(v, "new_rule")->isEnabled());

        child<QLineEdit>(v, "goto")->setText("$9000");
        emit child<QLineEdit>(v, "goto")->returnPressed();
        CHECK_FALSE(pixels->pixelRect(0x9000).isEmpty()); // at 1:1 all of WRAM fits, so nothing to scroll
        pixels->setZoom(8);
        emit child<QLineEdit>(v, "goto")->returnPressed();
        CHECK(pixels->firstVisibleOffset() <= 0x9000u);
        CHECK(pixels->firstVisibleOffset() + uint32_t(pixels->columns()) > 0x9000u);

        choose(v, "target", "vram");
        v.refresh();
        child<QPushButton>(v, "find_copies")->click();
        auto copies = child<QListWidget>(v, "copies");
        emit copies->itemActivated(copies->item(0));
        v.refresh();
        CHECK(pixels->snapshot().target == Target::WRAM);
        CHECK(pixels->selection() == std::pair{ 0x1C00u, 0x1C00u + 543u });
        CHECK(child<QPushButton>(v, "name_selection")->isEnabled());
        CHECK(v.nameSelection("sprite copy").isEmpty());
        CHECK(src.named.at("sprite copy").first == 0x1C00u);
    }

    TEST_CASE("zoom buttons and the zoom label")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        v.refresh();
        choose(v, "mode", "pixels");
        auto pixels = child<PixelView>(v, "pixels");
        int before = pixels->zoom();
        child<QPushButton>(v, "zoom_in")->click();
        CHECK(pixels->zoom() > before);
        CHECK(child<QLabel>(v, "zoom")->text().contains(QString::number(pixels->zoom())));
        for (int i = 0; i < 10; i++)
            child<QPushButton>(v, "zoom_out")->click();
        CHECK(pixels->zoom() == 1);
        CHECK_FALSE(child<QPushButton>(v, "zoom_out")->isEnabled());
        CHECK(child<QPushButton>(v, "zoom_in")->isEnabled());
    }

    TEST_CASE("hovering a pixel shows its address and value")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        v.refresh();
        choose(v, "mode", "pixels");
        auto pixels = child<PixelView>(v, "pixels");
        QApplication::processEvents();
        QPoint at = pixels->pixelRect(0x41).center();
        QMouseEvent move(QEvent::MouseMove, QPointF(at), pixels->viewport()->mapToGlobal(QPointF(at)), Qt::NoButton,
                         Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(pixels->viewport(), &move);
        auto hover = child<QLabel>(v, "hover")->text();
        CHECK(hover.contains("0x0041"));
        CHECK(hover.contains("0x11"));
        CHECK_FALSE(hover.contains("crash")); // the map knows nothing there
    }

    TEST_CASE("hovering a WRAM byte shows how often the game reads and writes it")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        v.refresh();
        choose(v, "mode", "pixels");
        auto pixels = child<PixelView>(v, "pixels");
        QApplication::processEvents();
        auto hover = [&](uint32_t offset) {
            pixels->onHover(offset);
            return child<QLabel>(v, "hover")->text();
        };
        CHECK(hover(0x2000).contains("read 3.0, written 0.5 a frame"));
        CHECK(hover(0x2001).contains("read 1234, written 0 a frame"));
        CHECK(hover(0x2002).contains("read 0, written 0 a frame"));
        choose(v, "target", "vram");
        v.refresh();
        CHECK_FALSE(hover(0x2000).contains("read")); // only WRAM is counted
    }

    TEST_CASE("hovering a byte the crash map knows shows its block's counts")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        v.refresh();
        choose(v, "mode", "pixels");
        auto pixels = child<PixelView>(v, "pixels");
        QApplication::processEvents();
        pixels->onHover(0x1F35);
        auto hover = child<QLabel>(v, "hover")->text();
        CHECK(hover.contains("0x1F35"));
        CHECK(hover.contains("block 0x1F30"));
        CHECK(hover.contains("5 crashes"));
        CHECK(hover.contains("12 calm"));
        CHECK(hover.contains("0.26"));
        CHECK(hover.contains("hot"));
    }

    TEST_CASE("the row width box sets the pixel rows, per memory")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        v.refresh();
        auto width = child<QSpinBox>(v, "row_width");
        CHECK_FALSE(width->isVisible());
        choose(v, "mode", "pixels");
        CHECK(width->isVisible());
        CHECK(width->value() == 0);
        CHECK(width->text().contains("Auto"));
        auto pixels = child<PixelView>(v, "pixels");

        width->setValue(48);
        CHECK(pixels->rowWidth() == 48);
        CHECK(pixels->columns() == 48);
        width->stepBy(1); // the arrows step one byte, to hunt for the width that lines up
        CHECK(pixels->columns() == 49);

        choose(v, "target", "vram");
        v.refresh();
        CHECK(width->value() == 0); // vram has its own, still auto
        width->setValue(64);
        CHECK(pixels->columns() == 64);
        choose(v, "target", "wram");
        v.refresh();
        CHECK(width->value() == 49);
        CHECK(pixels->columns() == 49);
        CHECK(pixels->snapshot().target == Target::WRAM);
    }

    TEST_CASE("a picture of the viewer in pixel mode")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        v.refresh();
        choose(v, "mode", "pixels");
        child<QSpinBox>(v, "row_width")->setValue(64);
        auto map = child<QListWidget>(v, "map");
        map->setCurrentRow(1);
        emit map->itemActivated(map->item(1));
        if (auto dir = getenv("CORRUPT_SHOTS_DIR"))
        {
            QDir().mkpath(dir);
            v.grab().save(QString("%1/memory-viewer-pixels.png").arg(dir));
            choose(v, "overlay", "none");
            v.grab().save(QString("%1/memory-viewer-pixels-plain.png").arg(dir));
        }
    }
}

TEST_SUITE("the tile view in the viewer")
{
    TEST_CASE("tiles mode shows its own controls and carries the selection over")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        choose(v, "target", "vram");
        v.refresh();
        auto tiles = child<TileView>(v, "tiles");
        auto format = child<QComboBox>(v, "tile_format");
        auto palette = child<QSpinBox>(v, "palette");
        auto per_row = child<QSpinBox>(v, "tiles_per_row");
        CHECK_FALSE(format->isVisible());

        child<MemoryView>(v, "view")->select(0x2010, 0x2010);
        choose(v, "mode", "tiles");
        CHECK(tiles->isVisible());
        CHECK(format->isVisible());
        CHECK(palette->isVisible());
        CHECK(per_row->isVisible());
        CHECK(child<QPushButton>(v, "zoom_in")->isVisible());
        CHECK_FALSE(child<QSpinBox>(v, "row_width")->isVisible());
        CHECK(tiles->snapshot().target == Target::VRAM);
        CHECK(tiles->selection() == std::pair{ 0x2000u, 0x201Fu }); // widened to its 4 bpp tile
        CHECK(child<QLabel>(v, "selection")->text().contains("0x2000"));
        CHECK(v.ruleFromSelection().range.last == 0x201Fu);

        choose(v, "tile_format", "2bpp");
        CHECK(tiles->format() == TileFormat::Bpp2);
        CHECK(palette->maximum() == 63);
        palette->setValue(5);
        CHECK(tiles->paletteRow() == 5);
        palette->setValue(palette->minimum());
        CHECK(tiles->paletteRow() == TileView::kGrey);
        CHECK(palette->text().contains("Grey"));
        choose(v, "tile_format", "8bpp");
        CHECK(palette->maximum() == 0);
        per_row->setValue(32);
        CHECK(tiles->tilesPerRow() == 32);

        int before = tiles->zoom();
        child<QPushButton>(v, "zoom_in")->click();
        CHECK(tiles->zoom() == before + 1);
        CHECK(child<QLabel>(v, "zoom")->text().contains(QString::number(tiles->zoom())));
    }

    TEST_CASE("the palette is read live while tiles show")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        v.refresh();
        int before = src.palette_reads;
        v.refresh();
        CHECK(src.palette_reads == before); // not in hex mode
        choose(v, "mode", "tiles");
        v.refresh();
        CHECK(src.palette_reads > before);
    }

    TEST_CASE("clicking a tile area in the map sets its format and palette")
    {
        FakeSource src;
        src.vram_live[0].tile_format = TileFormat::Bpp2;
        src.vram_live[0].palette = 8;
        MemoryViewer v(src);
        v.show();
        choose(v, "target", "vram");
        choose(v, "mode", "tiles");
        v.refresh();
        auto map = child<QListWidget>(v, "map");
        emit map->itemActivated(map->item(0));
        auto tiles = child<TileView>(v, "tiles");
        CHECK(tiles->format() == TileFormat::Bpp2);
        CHECK(tiles->paletteRow() == 8);
        CHECK(child<QComboBox>(v, "tile_format")->currentData().toString() == "2bpp");
        CHECK(child<QSpinBox>(v, "palette")->value() == 8);
        CHECK(tiles->selection() == std::pair{ 0x0000u, 0x3FFFu });

        emit map->itemActivated(map->item(1)); // a tile map: not graphics, the format stays
        CHECK(tiles->format() == TileFormat::Bpp2);
        CHECK(tiles->selection()->first == 0x8000u);
    }

    TEST_CASE("hovering a tile shows its number and address")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        choose(v, "target", "vram");
        choose(v, "mode", "tiles");
        v.refresh();
        QApplication::processEvents();
        auto tiles = child<TileView>(v, "tiles");
        QPoint at = tiles->tileRect(5).center();
        QMouseEvent move(QEvent::MouseMove, QPointF(at), tiles->viewport()->mapToGlobal(QPointF(at)), Qt::NoButton,
                         Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(tiles->viewport(), &move);
        auto hover = child<QLabel>(v, "hover")->text();
        CHECK(hover.contains("tile 0x005"));
        CHECK(hover.contains("0x00A0"));
    }

    TEST_CASE("a picture of the viewer in tiles mode")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        choose(v, "target", "vram");
        choose(v, "mode", "tiles");
        v.refresh();
        auto tiles = child<TileView>(v, "tiles");
        tiles->select(0x100, 0x1FF);
        QApplication::processEvents();
        CHECK(tiles->horizontalScrollBar()->maximum() == 0); // 16 tiles fit: no sideways scrolling
        CHECK_FALSE(child<QLineEdit>(v, "goto")->width() < 80); // the toolbar isn't squeezed
        if (auto dir = getenv("CORRUPT_SHOTS_DIR"))
        {
            QDir().mkpath(dir);
            v.grab().save(QString("%1/memory-viewer-tiles.png").arg(dir));
        }
    }
}
