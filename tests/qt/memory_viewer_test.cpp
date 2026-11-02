#include "doctest/doctest.h"
#include "MemoryViewer.hpp"
#include "UiKit.hpp"
#include "MemoryView.hpp"
#include "PixelView.hpp"
#include "TileView.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMouseEvent>
#include <QScrollBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>

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
        if (s.target == Target::ROM)
        {
            // JSL $C30006 at 0x1000, run; a data table at 0x2000; graphics at 0x3000.
            s.code.assign(size, 0);
            const uint8_t jsl[] = { 0x22, 0x06, 0x00, 0xC3 };
            std::copy(jsl, jsl + 4, s.bytes.begin() + 0x1000);
            s.code[0x1000] = codeflag::Opcode;
            s.code[0x1001] = s.code[0x1002] = s.code[0x1003] = codeflag::Operand;
            s.code[0x2000] = codeflag::Data;
            s.code[0x3000] = codeflag::Dma;
            s.rom_where.chunks.assign(size / RomAddresses::kChunk, RomAddresses::kNone);
            s.rom_where.chunks[1] = 0xC01000;
            s.rom_changes = { { 0x1001, 0x07, 340 }, { 0x4000, 0x00, 0 } };
            // JMP ($1100,X) at 0x1050, through a table at 0x1100: one entry used, one likely.
            const uint8_t jmp[] = { 0x7C, 0x00, 0x11 };
            std::copy(jmp, jmp + 3, s.bytes.begin() + 0x1050);
            s.code[0x1050] = codeflag::Opcode;
            s.code[0x1051] = s.code[0x1052] = codeflag::Operand;
            JumpTable t;
            t.site = 0x1050;
            t.opcode = 0x7C;
            t.entries = { { 0x1100, 0xC01200, 12, true }, { 0x1102, 0xC01210, 0, false } };
            s.jump_tables = { t };
            s.jumps = jump_marks(s.jump_tables, size);
            s.live.assign(size, 0); // the JSL ran this frame; its table a second ago
            s.live[0x1000] = 255;
            s.live[0x2000] = 90;
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
    struct Paint
    {
        Target target;
        std::vector<uint32_t> offsets;
        uint8_t value;
    };
    std::vector<Paint> painted;
    std::vector<std::pair<Target, std::vector<std::pair<uint32_t, uint8_t>>>> set;
    std::string setBytes(Target t, const std::vector<std::pair<uint32_t, uint8_t>> &bytes) override
    {
        set.push_back({ t, bytes });
        return {};
    }
    std::string paint(Target t, const std::vector<uint32_t> &offsets, uint8_t value) override
    {
        painted.push_back({ t, offsets, value });
        return {};
    }
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

    // Paint's draft and history (#66).
    int strokes_ended = 0, applied = 0, undos = 0, redos = 0, discards = 0;
    std::vector<size_t> gone_to;
    std::vector<PaintHistory::Row> rows;
    size_t drafted = 0;
    bool watching = false, undoable = false, redoable = false;
    void endStroke() override { strokes_ended++; }
    std::string applyPaint() override
    {
        applied++;
        return {};
    }
    bool undoPaint() override { return ++undos, true; }
    bool redoPaint() override { return ++redos, true; }
    void goToPaint(size_t count) override { gone_to.push_back(count); }
    void discardDraft() override { discards++; }
    std::vector<PaintHistory::Row> paintRows() override { return rows; }
    bool canUndoPaint() override { return undoable; }
    bool canRedoPaint() override { return redoable; }
    size_t draftSize() override { return drafted; }
    bool paintWatching() override { return watching; }
    std::string news;
    std::string paintNews() override { return news; }
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
    CHECK(child<QLabel>(v, "selection")->text().contains("0x01C00"));
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
    CHECK(list->item(0)->text().contains("0x01C00"));

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
    CHECK(child<QLabel>(v, "hover")->text().contains("0x012345"));
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
        CHECK(list->item(0)->text().contains("0x00F00"));
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
        CHECK(map->item(0)->text().contains("direct page"));
        CHECK_FALSE(map->item(0)->toolTip().isEmpty());
        emit map->itemActivated(map->item(3));
        auto sel = child<MemoryView>(v, "view")->selection();
        REQUIRE(sel);
        CHECK(sel->first == 0x10000u);
        CHECK(sel->second == 0x1FFFFu);
    }

    TEST_CASE("only overlays that mean something for the memory are offered, and a legend explains the colours")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        auto overlay = child<QComboBox>(v, "overlay");
        auto hidden = [&](const char *o) {
            return static_cast<QListView *>(overlay->view())->isRowHidden(overlay->findData(QString(o)));
        };
        choose(v, "target", "vram");
        CHECK(hidden("code"));
        CHECK(hidden("reads"));
        CHECK(!hidden("activity"));
        choose(v, "target", "rom");
        CHECK(!hidden("code"));
        CHECK(!hidden("changes"));
        CHECK(hidden("writes"));
        choose(v, "overlay", "code");
        CHECK(child<QLabel>(v, "legend")->text().contains("code the game ran"));
        choose(v, "target", "wram");
        CHECK(overlay->currentData().toString() == "activity"); // Code means nothing for WRAM
        CHECK(!hidden("reads"));
        CHECK(child<QLabel>(v, "legend")->text().contains("Hover"));
        CHECK(!child<QPushButton>(v, "clear_hits")->isVisible());
        choose(v, "overlay", "hits");
        CHECK(child<QPushButton>(v, "clear_hits")->isVisible());
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
        CHECK_FALSE(child<QAbstractButton>(v, "zoom_in")->isVisible());

        hex->scrollTo(0x4000);
        hex->select(0x4010, 0x404F);
        choose(v, "mode", "pixels");
        CHECK(pixels->isVisible());
        CHECK_FALSE(hex->isVisible());
        CHECK(child<QAbstractButton>(v, "zoom_in")->isVisible());
        CHECK(pixels->snapshot().bytes.size() == 0x20000);
        CHECK(pixels->selection() == std::pair{ 0x4010u, 0x404Fu });
        CHECK(pixels->firstVisibleOffset() <= 0x4000u);
        CHECK_FALSE(pixels->pixelRect(0x4010).isEmpty());
        CHECK(child<QLabel>(v, "selection")->text().contains("0x04010"));

        pixels->select(0x8000, 0x80FF);
        choose(v, "mode", "hex");
        CHECK(hex->selection() == std::pair{ 0x8000u, 0x80FFu });
        CHECK_FALSE(hex->cellRect(0x8000).isEmpty());
    }

    TEST_CASE("Paint: the toolbar's value and brush go to the pixel view; strokes reach the game")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        v.refresh();
        choose(v, "mode", "pixels");
        auto pixels = child<PixelView>(v, "pixels");
        auto paint = child<QPushButton>(v, "paint");
        CHECK(paint->isVisible());
        CHECK_FALSE(child<QSpinBox>(v, "paint_value")->isVisible()); // the paint tools show while painting
        CHECK_FALSE(child<QSpinBox>(v, "brush")->isVisible());
        paint->click();
        CHECK(pixels->paintMode());
        CHECK(child<QSpinBox>(v, "paint_value")->isVisible());
        CHECK(child<QSpinBox>(v, "brush")->isVisible());
        child<QSpinBox>(v, "paint_value")->setValue(0xEA);
        child<QSpinBox>(v, "brush")->setValue(2);
        pixels->setRowWidth(64);
        QPoint at = pixels->pixelRect(0x200).center();
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(at), pixels->viewport()->mapToGlobal(QPointF(at)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(pixels->viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(at), pixels->viewport()->mapToGlobal(QPointF(at)),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(pixels->viewport(), &release);
        REQUIRE(src.painted.size() == 1);
        CHECK(src.painted[0].target == Target::WRAM);
        CHECK(src.painted[0].value == 0xEA);
        CHECK(src.painted[0].offsets.size() == 4); // a 2x2 brush

        pixels->onPick(0x33); // as an alt-click does
        CHECK(child<QSpinBox>(v, "paint_value")->value() == 0x33);
        paint->click();
        CHECK_FALSE(pixels->paintMode());
        CHECK_FALSE(child<QSpinBox>(v, "brush")->isVisible());
        choose(v, "mode", "hex");
        CHECK_FALSE(paint->isVisible());
    }

    TEST_CASE("ROM paint is drafted: Apply Paint and Throw Away show for the ROM only, and a stroke ends on release")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        choose(v, "target", "rom");
        choose(v, "mode", "pixels");
        v.refresh();
        child<QPushButton>(v, "paint")->click();
        auto apply = child<QPushButton>(v, "apply_paint");
        auto discard = child<QPushButton>(v, "discard_draft");
        auto drafted = child<QLabel>(v, "draft_status");
        CHECK(apply->isVisible());
        CHECK_FALSE(apply->isEnabled()); // nothing drafted yet
        CHECK(drafted->text().contains("Paint here is a draft"));

        auto pixels = child<PixelView>(v, "pixels");
        QPoint at = pixels->pixelRect(0x200).center();
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(at), pixels->viewport()->mapToGlobal(QPointF(at)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(pixels->viewport(), &press);
        CHECK(src.strokes_ended == 0);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(at), pixels->viewport()->mapToGlobal(QPointF(at)),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(pixels->viewport(), &release);
        CHECK(src.strokes_ended == 1);

        src.drafted = 12;
        v.refresh();
        CHECK(apply->isEnabled());
        CHECK(drafted->text().contains("12 bytes drafted"));
        apply->click();
        CHECK(src.applied == 1);
        discard->click();
        CHECK(src.discards == 1);
        src.drafted = 0;
        src.watching = true;
        v.refresh();
        CHECK(drafted->text().contains("Watching"));
        src.watching = false; // ...and it crashed: the page says so, in the warning colour
        src.drafted = 12;
        src.news = "Your paint crashed the game (CPU stopped): it's back in the draft. Undo a stroke or two and apply again.";
        v.refresh();
        CHECK(drafted->text().contains("crashed the game"));
        CHECK(drafted->palette().color(QPalette::WindowText) == ui::toneColor(ui::Tone::Warning, drafted->palette()));
        CHECK(ui::audit(&v).isEmpty());

        choose(v, "target", "wram"); // painted live: nothing to apply
        v.refresh();
        CHECK_FALSE(apply->isVisible());
        CHECK_FALSE(discard->isVisible());
    }

    TEST_CASE("Undo and Redo: buttons, Cmd+Z and Shift+Cmd+Z, and the history list goes back to a row")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        choose(v, "target", "rom");
        choose(v, "mode", "pixels");
        child<QPushButton>(v, "paint")->click();
        PaintHistory::Row draft;
        draft.kind = PaintEntry::Draft;
        draft.target = Target::ROM;
        draft.bytes = 4;
        draft.first = 0x200;
        draft.value = 0xEA;
        draft.index = 2;
        PaintHistory::Row applied;
        applied.kind = PaintEntry::Applied;
        applied.target = Target::ROM;
        applied.strokes = 3;
        applied.bytes = 54;
        applied.first = 0x1000;
        applied.index = 1;
        PaintHistory::Row undone = draft;
        undone.active = false;
        undone.index = 3;
        src.rows = { undone, draft, applied };
        src.undoable = src.redoable = true;
        v.refresh();
        auto history = child<QListWidget>(v, "paint_history");
        CHECK(history->isVisible());
        REQUIRE(history->count() == 3);
        CHECK(history->item(1)->text().contains("4 bytes at 0x000200"));
        CHECK(history->item(1)->text().contains("EA"));
        CHECK(history->item(2)->text().contains("Applied"));
        CHECK(history->item(2)->text().contains("3 strokes"));
        CHECK(history->item(0)->foreground().color() != history->item(1)->foreground().color()); // undone: quieter

        child<QPushButton>(v, "undo_paint")->click();
        CHECK(src.undos == 1);
        child<QPushButton>(v, "redo_paint")->click();
        CHECK(src.redos == 1);
        auto undo_action = v.findChild<QAction *>("undo_paint_key");
        auto redo_action = v.findChild<QAction *>("redo_paint_key");
        REQUIRE(undo_action);
        REQUIRE(redo_action);
        CHECK(undo_action->shortcut() == QKeySequence(QKeySequence::Undo));
        CHECK(redo_action->shortcuts().contains(QKeySequence(QKeySequence::Redo)));
        undo_action->trigger();
        CHECK(src.undos == 2);
        history->itemClicked(history->item(2)); // just after the applied batch
        REQUIRE(src.gone_to.size() == 1);
        CHECK(src.gone_to[0] == 2);

        src.undoable = src.redoable = false;
        v.refresh();
        CHECK_FALSE(child<QPushButton>(v, "undo_paint")->isEnabled());
        child<QPushButton>(v, "paint")->click(); // off: no history to look at
        CHECK_FALSE(history->isVisible());
    }

    TEST_CASE("the pixel view marks drafted bytes")
    {
        PixelView view;
        view.resize(400, 300);
        MemorySnapshot s;
        s.target = Target::ROM;
        s.bytes.assign(0x400, 0x40);
        view.setSnapshot(s);
        view.setRowWidth(32);
        view.show();
        QApplication::processEvents();
        QPoint drafted_at = view.pixelRect(0x21).center(), plain_at = view.pixelRect(0x22).center();
        auto before = view.viewport()->grab().toImage();
        CHECK(before.pixelColor(drafted_at) == before.pixelColor(plain_at));
        s.draft = { 0x21 };
        view.setSnapshot(s);
        auto after = view.viewport()->grab().toImage();
        CHECK(after.pixelColor(drafted_at) != after.pixelColor(plain_at));
    }

    TEST_CASE("Export PNG writes the selection as a picture; an edited copy comes back as paint")
    {
        QTemporaryDir dir;
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        v.refresh();
        choose(v, "mode", "pixels");
        auto pixels = child<PixelView>(v, "pixels");
        CHECK(child<QPushButton>(v, "export_png")->isVisible());
        pixels->setRowWidth(32);
        pixels->selectRect(0x400, 8, 4);
        QString path = dir.filePath("rect.png");
        v.exportPng(path);
        QImage img(path);
        REQUIRE(img.width() == 8);
        REQUIRE(img.height() == 4);

        img.setPixel(1, 2, qRgb(0x7E, 0x7E, 0x7E)); // an edit "in Photoshop"
        QString edited = dir.filePath("edited.png");
        REQUIRE(img.save(edited));
        pixels->clearSelection(); // it finds its place from the picture, not the selection
        v.importPng(edited);
        REQUIRE(src.set.size() == 1);
        CHECK(src.set[0].first == Target::WRAM);
        REQUIRE(src.set[0].second.size() == 1);
        CHECK(src.set[0].second[0].first == 0x400 + 2 * 32 + 1);
        CHECK(src.set[0].second[0].second == 0x7E);
        CHECK(child<QLabel>(v, "selection")->text().contains("1"));

        // Nothing selected: the whole memory, in the current row width.
        v.exportPng(dir.filePath("all.png"));
        QImage all(dir.filePath("all.png"));
        CHECK(all.width() == 32);
        CHECK(all.height() == int(nominal_size(Target::WRAM) / 32));

        // A picture without its place goes to the selection, if it's the right size.
        QImage bare(8, 4, QImage::Format_Grayscale8);
        bare.fill(0);
        QString bare_path = dir.filePath("bare.png");
        REQUIRE(bare.save(bare_path));
        pixels->selectRect(0x800, 8, 4);
        v.importPng(bare_path);
        CHECK(src.set.size() == 2);
        CHECK(src.set[1].second.size() > 0);
        pixels->selectRect(0x800, 9, 4); // wrong size: refused, says why
        v.importPng(bare_path);
        CHECK(src.set.size() == 2);
        CHECK(child<QLabel>(v, "selection")->text().contains("8x4"));
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
        CHECK(r.name == "wram 16x8 at 0x01000");
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
        child<QAbstractButton>(v, "zoom_in")->click();
        CHECK(pixels->zoom() > before);
        CHECK(child<QLabel>(v, "zoom")->text().contains(QString::number(pixels->zoom())));
        for (int i = 0; i < 10; i++)
            child<QAbstractButton>(v, "zoom_out")->click();
        CHECK(pixels->zoom() == 1);
        CHECK_FALSE(child<QAbstractButton>(v, "zoom_out")->isEnabled());
        CHECK(child<QAbstractButton>(v, "zoom_in")->isEnabled());
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
        CHECK(hover.contains("0x00041"));
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

    TEST_CASE("hovering a ROM byte shows the instruction it's part of, or that it's data")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        choose(v, "target", "rom");
        v.refresh();
        choose(v, "mode", "pixels");
        auto pixels = child<PixelView>(v, "pixels");
        QApplication::processEvents();
        auto hover = [&](uint32_t offset) {
            pixels->onHover(offset);
            return child<QLabel>(v, "hover")->text();
        };
        auto code = hover(0x1002);
        CHECK(code.contains("$C0:1000"));
        CHECK(code.contains("22 06 00 C3"));
        CHECK(code.contains("JSL $C30006"));
        CHECK(code.contains("jump"));
        CHECK(hover(0x2000).contains("data"));
        CHECK(hover(0x3000).contains("DMA"));
        auto plain = hover(0x4800);
        CHECK_FALSE(plain.contains("data"));
        CHECK_FALSE(plain.contains("JSL"));
        for (auto o : { "code", "data", "dma", "changes" })
            CHECK(child<QComboBox>(v, "overlay")->findData(QString(o)) >= 0);
        CHECK(hover(0x1001).contains("changed from 0x07, used in 340 frames"));
        CHECK(hover(0x4000).contains("changed from 0x00, not used yet"));
        CHECK_FALSE(hover(0x1002).contains("changed"));
    }

    TEST_CASE("hovering a jump table entry says whose it is and where it goes; Jumps shows them, for the ROM only")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        choose(v, "target", "rom");
        v.refresh();
        choose(v, "mode", "pixels");
        auto pixels = child<PixelView>(v, "pixels");
        QApplication::processEvents();
        auto hover = [&](uint32_t offset) {
            pixels->onHover(offset);
            return child<QLabel>(v, "hover")->text();
        };
        auto used = hover(0x1101);
        CHECK(used.contains("entry 1 of 2"));
        CHECK(used.contains("JMP ($1100,X)"));
        CHECK(used.contains("$C0:1050"));
        CHECK(used.contains("goes to $C0:1200"));
        CHECK(used.contains("jumped through 12 times"));
        CHECK(hover(0x1102).contains("likely"));
        CHECK(hover(0x1050).contains("jumps through a table of routines: 2 entries"));
        CHECK_FALSE(hover(0x1104).contains("jump table"));

        auto overlay = child<QComboBox>(v, "overlay");
        auto hidden = [&](const char *o) {
            return static_cast<QListView *>(overlay->view())->isRowHidden(overlay->findData(QString(o)));
        };
        CHECK_FALSE(hidden("jumps"));
        choose(v, "overlay", "jumps");
        CHECK(child<QLabel>(v, "legend")->text().contains("jump tables"));
        choose(v, "target", "wram");
        CHECK(hidden("jumps"));
        CHECK(overlay->currentData().toString() == "activity");
    }

    TEST_CASE("Live shows what the game uses right now, for the ROM only, and the hover says how recently")
    {
        FakeSource src;
        MemoryViewer v(src);
        v.show();
        choose(v, "target", "rom");
        v.refresh();
        choose(v, "mode", "pixels");
        auto pixels = child<PixelView>(v, "pixels");
        QApplication::processEvents();
        auto hover = [&](uint32_t offset) {
            pixels->onHover(offset);
            return child<QLabel>(v, "hover")->text();
        };
        CHECK(hover(0x1000).contains("in use right now"));
        CHECK(hover(0x2000).contains("used in the last second or two"));
        CHECK_FALSE(hover(0x3000).contains("use"));
        auto overlay = child<QComboBox>(v, "overlay");
        auto hidden = [&](const char *o) {
            return static_cast<QListView *>(overlay->view())->isRowHidden(overlay->findData(QString(o)));
        };
        CHECK_FALSE(hidden("live"));
        choose(v, "overlay", "live");
        CHECK(child<QLabel>(v, "legend")->text().contains("right now"));
        if (auto dir = getenv("CORRUPT_SHOTS_DIR"))
            v.grab().save(QString("%1/viewer-rom-live.png").arg(dir));
        choose(v, "target", "vram");
        CHECK(hidden("live"));
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
        CHECK(hover.contains("0x01F35"));
        CHECK(hover.contains("block 0x01F30"));
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
        CHECK(child<QAbstractButton>(v, "zoom_in")->isVisible());
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
        child<QAbstractButton>(v, "zoom_in")->click();
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

TEST_CASE("nothing in it is unnamed for a screen reader")
{
    FakeSource src;
    MemoryViewer v(src);
    auto issues = ui::audit(&v);
    CAPTURE(issues.join("\n").toStdString());
    CHECK(issues.isEmpty());
}
