#include "doctest/doctest.h"
#include "SurvivorsDialog.hpp"
#include "UiKit.hpp"
#include "corrupt/code_map.h"

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>

using namespace corrupt;

namespace
{
struct FakeSurvivors : SurvivorSource
{
    std::vector<Survivor> list;
    std::vector<Pin> pinned;
    std::vector<uint32_t> last_pin, last_unpin;
    std::string last_note;

    FakeSurvivors()
    {
        Survivor branch;
        branch.offset = 0x21A3F;
        branch.address = 0xC21A3F;
        branch.was = 0xD0;
        branch.now = 0xF0;
        branch.frames_used = 4120;
        branch.flags = codeflag::Opcode;
        branch.before = "BNE $1A4D";
        branch.after = "BEQ $1A4D";
        Survivor data;
        data.offset = 0x30000;
        data.was = 0x10;
        data.now = 0x11;
        data.frames_used = 150;
        data.flags = codeflag::Data;
        list = { branch, data };
        pinned = { { 0x500, 0xEA, 0x20, "no sound" } };
    }
    std::vector<Survivor> survivors() override { return list; }
    std::vector<Pin> pins() override { return pinned; }
    std::string pin(const std::vector<uint32_t> &offsets, const std::string &note) override
    {
        last_pin = offsets;
        last_note = note;
        return {};
    }
    void unpin(const std::vector<uint32_t> &offsets) override { last_unpin = offsets; }
};

template <typename W>
W *child(QWidget &w, const char *name)
{
    auto c = w.findChild<W *>(name);
    REQUIRE_MESSAGE(c, name);
    return c;
}
} // namespace

TEST_CASE("the survivors dialog lists what proved itself, pins it with a note, and unpins")
{
    FakeSurvivors src;
    SurvivorsDialog d(src);
    d.show();
    QApplication::processEvents();
    auto table = child<QTableWidget>(d, "survivors");
    REQUIRE(table->rowCount() == 2);
    CHECK(table->item(0, 0)->text() == "0x021A3F");
    CHECK(table->item(0, 0)->toolTip() == "CPU address $C2:1A3F");
    CHECK(table->item(0, 1)->text() == "D0 → F0");
    CHECK(table->item(0, 2)->text() == "BNE $1A4D → BEQ $1A4D");
    CHECK(table->item(0, 3)->text().contains("4"));
    CHECK(table->item(1, 0)->text() == "0x030000");
    CHECK(table->item(1, 2)->text() == "data");

    auto pin = child<QPushButton>(d, "pin");
    CHECK(!pin->isEnabled()); // nothing selected
    table->selectRow(0);
    CHECK(pin->isEnabled());
    child<QLineEdit>(d, "note")->setText("doors stay shut");
    pin->click();
    CHECK(src.last_pin == std::vector<uint32_t>{ 0x21A3F });
    CHECK(src.last_note == "doors stay shut");

    auto pins = child<QTableWidget>(d, "pins");
    REQUIRE(pins->rowCount() == 1);
    CHECK(pins->item(0, 0)->text() == "0x000500");
    CHECK(pins->item(0, 1)->text() == "20 → EA");
    CHECK(pins->item(0, 2)->text() == "no sound");
    pins->selectRow(0);
    child<QPushButton>(d, "unpin")->click();
    CHECK(src.last_unpin == std::vector<uint32_t>{ 0x500 });

    if (auto dir = getenv("CORRUPT_SHOTS_DIR"))
    {
        QDir().mkpath(dir);
        d.grab().save(QString("%1/survivors-dialog.png").arg(dir));
    }

    src.list.clear();
    src.pinned.clear();
    d.refresh();
    CHECK(table->rowCount() == 0);
    CHECK(child<QLabel>(d, "empty")->isVisible());
    // The empty state reads as help, not as something switched off, and nothing to act on shows.
    CHECK(child<QLabel>(d, "empty")->isEnabled());
    CHECK_FALSE(table->isVisible());
    CHECK_FALSE(child<QLineEdit>(d, "note")->isVisible());
    CHECK_FALSE(child<QTableWidget>(d, "pins")->isVisible());
    CHECK(child<QLabel>(d, "pins_empty")->isVisible());
    if (auto dir = getenv("CORRUPT_SHOTS_DIR"))
        d.grab().save(QString("%1/survivors-dialog-empty.png").arg(dir));
}

TEST_CASE("nothing in it is unnamed for a screen reader")
{
    FakeSurvivors src;
    SurvivorsDialog d(src);
    auto issues = ui::audit(&d);
    CAPTURE(issues.join("\n").toStdString());
    CHECK(issues.isEmpty());
}
