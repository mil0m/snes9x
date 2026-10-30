#include "doctest/doctest.h"
#include "MemoryViewer.hpp"
#include "MemoryView.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

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
        s.heat.assign(size, 0.0f);
        s.hits.assign(size, 0);
        for (size_t i = 0x1C00; i < std::min<size_t>(0x1E20, size); i++)
            s.heat[i] = 20.0f;
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
    Regions regions() override { return {}; }
    std::string addRule(const Rule &) override { return {}; }
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

TEST_CASE("overlay choice and clearing hits")
{
    FakeSource src;
    MemoryViewer v(src);
    v.show();
    choose(v, "overlay", "hits");
    child<QPushButton>(v, "clear_hits")->click();
    CHECK(src.clears == 1);
}
