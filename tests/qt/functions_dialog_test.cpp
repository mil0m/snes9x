#include "doctest/doctest.h"
#include "FunctionsDialog.hpp"
#include "fake_functions.h"
#include "UiKit.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>

using namespace corrupt;

namespace
{
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
    Size,
    Off,
    About
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
    CHECK(table->item(1, Frames)->toolTip().contains("8 places"));
    CHECK(table->item(0, Size)->text() == "46 B");
    CHECK(table->item(0, Size)->toolTip().contains("2 pieces"));
    CHECK(table->item(2, Size)->toolTip().contains("isn't known"));
    CHECK(table->item(0, Off)->checkState() == Qt::Unchecked);
    CHECK(table->item(1, Off)->checkState() == Qt::Checked);
    CHECK(table->item(0, About)->text() == "scroll (erik)");
    CHECK(table->item(1, About)->text().contains("gravity"));
    CHECK(table->item(1, About)->text().contains("key 3"));
    CHECK(table->item(1, About)->text().contains("protected with what it calls"));
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
    CHECK(!keep->isVisible()); // nothing selected: no details
    table->selectRow(0);
    CHECK(keep->isEnabled());
    CHECK(!forget->isEnabled()); // not in the recipe yet
    child<QComboBox>(d, "hotkey")->setCurrentIndex(5); // key 5
    child<QCheckBox>(d, "start_off")->setChecked(true);
    child<QLineEdit>(d, "name")->setText("no scroll");
    keep->click();
    REQUIRE(src.kept.size() == 1);
    CHECK(src.kept[0].entry == 0x0019EE);
    CHECK(src.kept[0].hotkey == 5);
    CHECK(src.kept[0].start_off);
    CHECK(src.kept[0].note == "no scroll");

    table->selectRow(1); // already kept: the controls show its switch
    CHECK(child<QComboBox>(d, "hotkey")->currentIndex() == 3);
    CHECK(child<QLineEdit>(d, "name")->text() == "gravity");
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
    filter->setText("erik"); // someone else's name finds it too
    CHECK(!table->isRowHidden(0));
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

TEST_CASE("the selected function: your name is saved, Protect guards it, Show in ROM selects its code")
{
    FakeFunctions src;
    FunctionsDialog d(src);
    d.show();
    auto table = child<QTableWidget>(d, "functions");
    auto details = child<QWidget>(d, "selected")->parentWidget();
    CHECK_FALSE(details->isVisible()); // nothing selected
    table->selectRow(0);
    CHECK(details->isVisible());
    CHECK(child<QLabel>(d, "selected")->text() == "$80:99EE · scroll (erik)");
    auto name = child<QLineEdit>(d, "name");
    CHECK(name->text().isEmpty()); // erik named it, you didn't
    name->setText("  camera ");
    emit name->editingFinished();
    REQUIRE(src.named.size() == 1);
    CHECK(src.named[0] == std::pair<uint32_t, std::string>{ 0x0019EE, "camera" });

    auto protect = child<QCheckBox>(d, "protect");
    auto deep = child<QCheckBox>(d, "deep");
    CHECK_FALSE(protect->isChecked());
    CHECK_FALSE(deep->isEnabled());
    protect->setChecked(true);
    REQUIRE(src.protects.size() == 1);
    CHECK(src.protects[0].entry == 0x0019EE);
    CHECK(src.protects[0].on);
    CHECK_FALSE(src.protects[0].deep);
    CHECK(deep->isEnabled());
    deep->setChecked(true);
    CHECK(src.protects.back().deep);

    std::pair<uint32_t, uint32_t> shown{};
    d.onShowInRom = [&](uint32_t first, uint32_t last) { shown = { first, last }; };
    child<QPushButton>(d, "show")->click();
    CHECK(shown == std::pair<uint32_t, uint32_t>{ 0x0019EE, 0x001A0F }); // the piece with the entry

    table->selectRow(1); // protected deep already: the boxes say so, and nothing is sent
    CHECK(protect->isChecked());
    CHECK(deep->isChecked());
    CHECK(src.protects.size() == 2);

    table->selectRow(2); // its code isn't known: nothing to protect or show
    CHECK_FALSE(protect->isEnabled());
    CHECK_FALSE(child<QPushButton>(d, "show")->isEnabled());

    if (auto dir = getenv("CORRUPT_SHOTS_DIR"))
    {
        table->selectRow(1);
        QApplication::processEvents();
        d.grab().save(QString("%1/functions-dialog-selected.png").arg(dir));
    }
}

TEST_CASE("a switch kept with a timer: off for a while, every so often")
{
    FakeFunctions src;
    FunctionsDialog d(src);
    d.show();
    child<QTableWidget>(d, "functions")->selectRow(0);
    child<QCheckBox>(d, "timer")->setChecked(true);
    child<QDoubleSpinBox>(d, "timer_for")->setValue(2);
    child<QDoubleSpinBox>(d, "timer_every")->setValue(10);
    child<QPushButton>(d, "keep")->click();
    REQUIRE(src.kept.size() == 1);
    CHECK(src.kept[0].for_frames == 120);
    CHECK(src.kept[0].every == 600);
    child<QCheckBox>(d, "timer")->setChecked(true); // (the fake doesn't keep the switch in its rows)
    child<QDoubleSpinBox>(d, "timer_for")->setValue(10);
    child<QDoubleSpinBox>(d, "timer_every")->setValue(10);
    child<QPushButton>(d, "keep")->click();
    CHECK(src.kept.size() == 1); // off as long as the cycle: refused
}
