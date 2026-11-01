#include "doctest/doctest.h"
#include "FunctionsDialog.hpp"
#include "UiKit.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>

using namespace corrupt;

namespace
{
struct FakeFunctions : FunctionSource
{
    std::vector<FunctionRow> rows;
    bool on = true;
    std::vector<uint32_t> flipped, forgotten;
    struct Kept
    {
        uint32_t entry;
        int hotkey;
        bool start_off;
        std::string note;
    };
    std::vector<Kept> kept;

    FakeFunctions()
    {
        FunctionRow busy;
        busy.f = { .entry = 0x0019EE, .type = CallType::Near, .calls = 17972, .callers = 1, .frames = 17972, .now = 300 };
        busy.address = 0x8099EE;
        FunctionRow lng;
        lng.f = { .entry = 0x000142, .type = CallType::Long, .calls = 31043, .callers = 8, .frames = 17779, .now = 150 };
        lng.address = 0x808142;
        lng.off = true;
        Switch s;
        s.entry = 0x000142;
        s.ret = kRtl;
        s.hotkey = 3;
        s.note = "gravity?";
        lng.kept = s;
        FunctionRow mixed;
        mixed.f = { .entry = 0x002000, .type = CallType::Mixed, .calls = 10, .callers = 2, .frames = 5, .now = 0 };
        mixed.address = 0x80A000;
        rows = { busy, lng, mixed };
    }
    std::vector<FunctionRow> functions() override { return rows; }
    bool running() override { return on; }
    std::string flip(uint32_t entry) override
    {
        flipped.push_back(entry);
        return {};
    }
    std::string keep(uint32_t entry, int hotkey, bool start_off, const std::string &note) override
    {
        kept.push_back({ entry, hotkey, start_off, note });
        return {};
    }
    void forget(uint32_t entry) override { forgotten.push_back(entry); }
};

template <typename W>
W *child(QWidget &w, const char *name)
{
    auto c = w.findChild<W *>(name);
    REQUIRE_MESSAGE(c, name);
    return c;
}

enum Col
{
    Name,
    Now,
    Frames,
    Callers,
    Off,
    Recipe
};
} // namespace

TEST_CASE("the functions window lists what runs, busiest now first, and shows which are off and kept")
{
    FakeFunctions src;
    FunctionsDialog d(src);
    d.show();
    QApplication::processEvents();
    auto table = child<QTableWidget>(d, "functions");
    REQUIRE(table->rowCount() == 3);
    CHECK(table->item(0, Name)->text() == "$80:99EE");
    CHECK(table->item(0, Name)->toolTip().contains("JSR"));
    CHECK(table->item(0, Now)->text() == "100%");
    CHECK(table->item(1, Now)->text() == "50%");
    CHECK(table->item(0, Frames)->text().remove(QRegularExpression("[^0-9]")) == "17972");
    CHECK(table->item(1, Callers)->text() == "8");
    CHECK(table->item(0, Off)->checkState() == Qt::Unchecked);
    CHECK(table->item(1, Off)->checkState() == Qt::Checked);
    CHECK(table->item(1, Recipe)->text().contains("3"));
    CHECK(table->item(1, Recipe)->text().contains("gravity?"));
    // A function reached both ways can't be skipped safely: no box to tick.
    CHECK(!(table->item(2, Off)->flags() & Qt::ItemIsUserCheckable));
    CHECK(child<QLabel>(d, "empty")->isHidden());

    if (auto dir = getenv("CORRUPT_SHOTS_DIR"))
    {
        QDir().mkpath(dir);
        d.grab().save(QString("%1/functions-dialog.png").arg(dir));
    }
}

TEST_CASE("ticking Off flips it; while corruption is off it says what to do instead")
{
    FakeFunctions src;
    FunctionsDialog d(src);
    d.show();
    auto table = child<QTableWidget>(d, "functions");
    table->item(0, Off)->setCheckState(Qt::Checked);
    CHECK(src.flipped == std::vector<uint32_t>{ 0x0019EE });
    CHECK(child<QWidget>(d, "message")->isVisible());

    src.on = false;
    d.refresh();
    CHECK(child<QWidget>(d, "not_running")->isVisible());
    table->item(0, Off)->setCheckState(Qt::Checked);
    CHECK(src.flipped.size() == 1); // nothing flipped
    CHECK(table->item(0, Off)->checkState() == Qt::Unchecked);
}

TEST_CASE("a switch is kept in the recipe with its hotkey, start and note, and forgotten again")
{
    FakeFunctions src;
    FunctionsDialog d(src);
    d.show();
    auto table = child<QTableWidget>(d, "functions");
    auto keep = child<QPushButton>(d, "keep");
    auto forget = child<QPushButton>(d, "forget");
    CHECK(!keep->isEnabled());
    table->selectRow(0);
    CHECK(keep->isEnabled());
    CHECK(!forget->isEnabled()); // not in the recipe yet
    child<QComboBox>(d, "hotkey")->setCurrentIndex(5); // key 5
    child<QCheckBox>(d, "start_off")->setChecked(true);
    child<QLineEdit>(d, "note")->setText("no scroll");
    keep->click();
    REQUIRE(src.kept.size() == 1);
    CHECK(src.kept[0].entry == 0x0019EE);
    CHECK(src.kept[0].hotkey == 5);
    CHECK(src.kept[0].start_off);
    CHECK(src.kept[0].note == "no scroll");

    table->selectRow(1); // already kept: the controls show its switch
    CHECK(child<QComboBox>(d, "hotkey")->currentIndex() == 3);
    CHECK(child<QLineEdit>(d, "note")->text() == "gravity?");
    CHECK(forget->isEnabled());
    forget->click();
    CHECK(src.forgotten == std::vector<uint32_t>{ 0x000142 });

    table->selectRow(2); // mixed: nothing to keep
    CHECK(!keep->isEnabled());
}

TEST_CASE("the filter finds a function by its address or its note")
{
    FakeFunctions src;
    FunctionsDialog d(src);
    d.show();
    auto table = child<QTableWidget>(d, "functions");
    auto filter = child<QLineEdit>(d, "filter");
    filter->setText("99ee");
    CHECK(!table->isRowHidden(0));
    CHECK(table->isRowHidden(1));
    filter->setText("gravity");
    CHECK(table->isRowHidden(0));
    CHECK(!table->isRowHidden(1));
    filter->clear();
    CHECK(!table->isRowHidden(2));
}

TEST_CASE("with nothing learned yet it says how to fill it")
{
    FakeFunctions src;
    src.rows.clear();
    FunctionsDialog d(src);
    d.show();
    CHECK(child<QLabel>(d, "empty")->isVisible());
    CHECK(child<QLabel>(d, "empty")->isEnabled());
    CHECK_FALSE(child<QTableWidget>(d, "functions")->isVisible());
    CHECK_FALSE(child<QPushButton>(d, "keep")->isVisible());
    if (auto dir = getenv("CORRUPT_SHOTS_DIR"))
        d.grab().save(QString("%1/functions-dialog-empty.png").arg(dir));
}

TEST_CASE("nothing in the functions window is unnamed for a screen reader")
{
    FakeFunctions src;
    FunctionsDialog d(src);
    auto issues = ui::audit(&d);
    CAPTURE(issues.join("\n").toStdString());
    CHECK(issues.isEmpty());
}
