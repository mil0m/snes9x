#include "doctest/doctest.h"
#include "FunctionsPage.hpp"
#include "fake_functions.h"
#include "UiKit.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QElapsedTimer>
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
    Guard,   // protected, or guarded anyway
    About,   // the names
    InPlay   // what you did with it
};
} // namespace

TEST_CASE("the functions window lists what runs, busiest now first, and shows which are off and kept")
{
    FakeFunctions src;
    FunctionsPage d(src);
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
    CHECK(table->item(1, About)->text() == "gravity"); // names only...
    CHECK(table->item(1, InPlay)->text().contains("button 3")); // ...and what you did, apart
    CHECK_FALSE(table->item(1, InPlay)->text().contains("protected")); // that's the Guard column's
    CHECK(table->item(1, Guard)->checkState() == Qt::Checked);
    CHECK(table->item(1, Guard)->text() == "+ calls");
    CHECK(table->item(0, Guard)->checkState() == Qt::Unchecked);
    CHECK(table->item(0, InPlay)->text().isEmpty());
    // Kept on a button later: the column grows to fit the new words.
    Switch s;
    s.entry = src.rows[0].f.entry;
    s.hotkey = 7;
    s.start_off = true;
    s.every = 600;
    s.for_frames = 120;
    src.rows[0].kept = s;
    src.rows[0].changes.resize(3);
    d.updateRows();
    CHECK(table->item(0, InPlay)->text().contains("button 7"));
    CHECK(table->fontMetrics().horizontalAdvance(table->item(0, InPlay)->text()) <= table->columnWidth(InPlay));
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
    FunctionsPage d(src);
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
    FunctionsPage d(src);
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
    FunctionsPage d(src);
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
    FunctionsPage d(src);
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
    FunctionsPage d(src);
    auto issues = ui::audit(&d);
    CAPTURE(issues.join("\n").toStdString());
    CHECK(issues.isEmpty());
}

TEST_CASE("the selected function: your name is saved, Protect guards it, Show in ROM selects its code")
{
    FakeFunctions src;
    FunctionsPage d(src);
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
    FunctionsPage d(src);
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

TEST_CASE("a game with hundreds of functions fills and updates quickly")
{
    FakeFunctions src;
    auto base = src.rows[0];
    src.rows.clear();
    for (uint32_t i = 0; i < 600; i++)
    {
        auto r = base;
        r.f.entry = 0x1000 + i * 0x20;
        r.f.frames = 600 - i;
        r.label.clear();
        src.rows.push_back(r);
    }
    QElapsedTimer clock;
    clock.start();
    FunctionsPage d(src);
    d.resize(900, 640);
    d.show();
    auto built = clock.restart();
    CHECK(built < 1500);
    for (int i = 0; i < 4; i++) // two seconds of updates while it's open
        d.updateRows();
    CHECK(clock.elapsed() < 400);
    auto table = d.findChild<QTableWidget *>("functions");
    REQUIRE(table);
    CHECK(table->rowCount() == 600);
    // The counts grow while it's open (600 to 1,234,567): they still fit their column.
    src.rows[0].f.frames = 1234567;
    d.updateRows();
    auto text = table->item(0, 2)->text();
    CHECK(table->fontMetrics().horizontalAdvance(text) + 8 <= table->columnWidth(2));
}

TEST_CASE("names are typed in the table: double-click, type, Return; Esc cancels; empty removes")
{
    FakeFunctions src;
    FunctionsPage d(src);
    d.show();
    QApplication::processEvents();
    auto table = child<QTableWidget>(d, "functions");
    // Only the name cells can be edited.
    CHECK(bool(table->item(1, About)->flags() & Qt::ItemIsEditable));
    for (int col : { Name, Now, Frames, Size, Off, Guard, InPlay })
        CHECK_FALSE(bool(table->item(1, col)->flags() & Qt::ItemIsEditable));

    auto edit = [&](int row) -> QLineEdit * {
        table->setCurrentCell(row, About);
        table->editItem(table->item(row, About));
        QApplication::processEvents();
        return table->viewport()->findChild<QLineEdit *>();
    };
    auto press = [](QWidget *w, int key) {
        QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(w, &down);
        QApplication::processEvents();
    };

    auto editor = edit(1);
    REQUIRE(editor);
    CHECK(editor->text() == "gravity"); // starts from your own name
    editor->setText("falling");
    press(editor, Qt::Key_Return);
    REQUIRE_FALSE(src.named.empty());
    CHECK(src.named.back() == std::pair<uint32_t, std::string>{ 0x000142, "falling" });
    CHECK(child<QLineEdit>(d, "name")->text() == "falling"); // the field under the table follows

    size_t before = src.named.size();
    editor = edit(0); // erik's name only: yours starts empty
    REQUIRE(editor);
    CHECK(editor->text().isEmpty());
    editor->setText("scroll x");
    press(editor, Qt::Key_Escape);
    CHECK(src.named.size() == before);

    editor = edit(1);
    REQUIRE(editor);
    editor->clear();
    press(editor, Qt::Key_Return);
    CHECK(src.named.back() == std::pair<uint32_t, std::string>{ 0x000142, "" });
}

TEST_CASE("Show picks which functions: running now, off, in play, guarded, named or not; with the search too")
{
    FakeFunctions src;
    src.rows[0].guarded = true; // guarded anyway: off a moment ago, or a protected caller
    FunctionRow quiet;          // ran once, long ago; nobody named it
    quiet.f = { .entry = 0x002400, .type = CallType::Near, .calls = 1, .callers = 1, .frames = 1, .now = 0 };
    quiet.address = 0x80A400;
    quiet.body_bytes = 8;
    src.rows.push_back(quiet);
    FunctionsPage d(src);
    d.show();
    auto table = child<QTableWidget>(d, "functions");
    auto show = child<QComboBox>(d, "show_filter");
    auto count = child<QLabel>(d, "count");
    auto visible = [&] {
        std::vector<uint32_t> out;
        for (int i = 0; i < table->rowCount(); i++)
            if (!table->isRowHidden(i))
                out.push_back(src.rows[size_t(i)].f.entry);
        return out;
    };
    auto pick = [&](const char *text) {
        int i = show->findText(text);
        REQUIRE_MESSAGE(i >= 0, text);
        show->setCurrentIndex(i);
    };
    CHECK(visible().size() == 4);
    CHECK_FALSE(count->isVisible()); // nothing hidden: no count
    pick("Running now");
    CHECK(visible() == std::vector<uint32_t>{ 0x0019EE, 0x000142 });
    CHECK(count->isVisible());
    CHECK(count->text() == "2 of 4");
    pick("Off");
    CHECK(visible() == std::vector<uint32_t>{ 0x000142 });
    pick("In play");
    CHECK(visible() == std::vector<uint32_t>{ 0x000142 });
    pick("Guarded");
    CHECK(visible() == std::vector<uint32_t>{ 0x0019EE, 0x000142 });
    pick("Named");
    CHECK(visible() == std::vector<uint32_t>{ 0x0019EE, 0x000142 });
    pick("Not named yet");
    CHECK(visible() == std::vector<uint32_t>{ 0x002000, 0x002400 });
    child<QLineEdit>(d, "filter")->setText("24"); // and the search
    CHECK(visible() == std::vector<uint32_t>{ 0x002400 });
    CHECK(count->text() == "1 of 4");
    pick("All functions");
    child<QLineEdit>(d, "filter")->clear();
    CHECK(visible().size() == 4);
    CHECK_FALSE(count->isVisible());
}

TEST_CASE("the Guard column: tick to protect, untick to stop; auto when guarded anyway; no box without its code")
{
    FakeFunctions src;
    src.rows[0].guarded = true;
    FunctionsPage d(src);
    d.show();
    auto table = child<QTableWidget>(d, "functions");
    CHECK(table->item(0, Guard)->text() == "auto");
    CHECK(table->item(0, Guard)->toolTip().contains("Guarded while"));
    CHECK(table->item(1, Guard)->toolTip().contains("with what it calls"));
    CHECK_FALSE(bool(table->item(2, Guard)->flags() & Qt::ItemIsUserCheckable)); // its code isn't known
    CHECK(table->item(2, Guard)->toolTip().contains("isn't known"));

    table->selectRow(0);
    table->item(0, Guard)->setCheckState(Qt::Checked);
    REQUIRE(src.protects.size() == 1);
    CHECK(src.protects[0].entry == 0x0019EE);
    CHECK(src.protects[0].on);
    CHECK_FALSE(src.protects[0].deep);
    CHECK(child<QWidget>(d, "message")->isVisible());

    table->item(1, Guard)->setCheckState(Qt::Unchecked);
    REQUIRE(src.protects.size() == 2);
    CHECK(src.protects[1].entry == 0x000142);
    CHECK_FALSE(src.protects[1].on);
}

TEST_CASE("a tick that waits for the next frame stays ticked (a paused game), and says so")
{
    FakeFunctions src;
    FunctionsPage d(src);
    d.show();
    auto table = child<QTableWidget>(d, "functions");
    table->item(0, Off)->setCheckState(Qt::Checked);
    REQUIRE(src.flipped.size() == 1);
    // The session reports the queued flip: off, pending.
    src.rows[0].off = true;
    src.rows[0].pending = true;
    d.updateRows();
    CHECK(table->item(0, Off)->checkState() == Qt::Checked);
    CHECK(table->item(0, Off)->toolTip().contains("next frame"));
    src.rows[0].pending = false; // the game ran
    d.updateRows();
    CHECK(table->item(0, Off)->checkState() == Qt::Checked);
    CHECK_FALSE(table->item(0, Off)->toolTip().contains("next frame"));
}
