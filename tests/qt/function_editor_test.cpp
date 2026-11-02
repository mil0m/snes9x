#include "doctest/doctest.h"
#include "FunctionEditor.hpp"
#include "fake_functions.h"
#include "UiKit.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>

namespace
{
template <typename W>
W *child(QWidget &w, const char *name)
{
    auto c = w.findChild<W *>(name);
    REQUIRE_MESSAGE(c, name);
    return c;
}

QString message(QWidget &w)
{
    return static_cast<ui::StatusLine *>(child<QWidget>(w, "message"))->text();
}

// The busy function, with two more near functions in its bank to call instead.
struct Editing
{
    FakeFunctions src;
    std::unique_ptr<FunctionEditor> editor;
    Editing()
    {
        FunctionRow other;
        other.f = { .entry = 0x002100, .type = CallType::Near, .calls = 50, .callers = 1, .frames = 50, .now = 10 };
        other.address = 0x80A100;
        other.label = "jump";
        FunctionRow far;
        far.f = { .entry = 0x012100, .type = CallType::Near, .calls = 50, .callers = 1, .frames = 50, .now = 10 };
        far.address = 0x81A100; // another bank: a JSR can't reach it
        src.rows.push_back(other);
        src.rows.push_back(far);
        src.busy_options();
        editor = std::make_unique<FunctionEditor>(src, src.rows[0]);
        editor->show();
        QApplication::processEvents();
    }
    QTableWidget *lines() { return child<QTableWidget>(*editor, "lines"); }
};
} // namespace

TEST_CASE("Change It lists the lines that can change and what each can become")
{
    Editing e;
    auto t = e.lines();
    REQUIRE(t->rowCount() == 5);
    CHECK(t->item(0, 0)->text() == "$80:99EE");
    CHECK(t->item(0, 1)->text() == "LDA #$04");
    CHECK(t->item(0, 2)->text() == "another number");
    CHECK(t->item(1, 2)->text() == "another number, or SBC");
    CHECK(t->item(2, 2)->text().contains("BNE"));
    CHECK(t->item(3, 2)->text() == "DEC");
    CHECK(t->item(4, 2)->text().contains("another function"));
    CHECK(t->item(0, 3)->text().isEmpty()); // as the game has it
    CHECK_FALSE(child<QWidget>(*e.editor, "number")->isVisible()); // nothing selected
    CHECK(child<QLabel>(*e.editor, "empty")->isHidden());
    if (auto dir = getenv("CORRUPT_SHOTS_DIR"))
    {
        QDir().mkpath(dir);
        t->selectRow(1);
        QApplication::processEvents();
        e.editor->grab().save(QString("%1/function-editor.png").arg(dir));
    }
}

TEST_CASE("a number: apply it, the line says what it is now, put it back")
{
    Editing e;
    e.lines()->selectRow(0);
    auto number = child<QSpinBox>(*e.editor, "number");
    CHECK(number->isVisible());
    CHECK(number->value() == 4);
    CHECK(number->maximum() == 0xFF);
    CHECK_FALSE(child<QCheckBox>(*e.editor, "swap_math")->isVisible()); // LDA has no other half
    number->setValue(1);
    child<QPushButton>(*e.editor, "apply")->click();
    REQUIRE(e.src.applied.size() == 1);
    auto &[c, on] = e.src.applied[0];
    CHECK(on);
    CHECK(c.function == 0x0019EE);
    CHECK(c.at == 0x19EE);
    CHECK(c.kind == ChangeKind::Number);
    CHECK(c.value == std::vector<uint8_t>{ 0xA9, 0x01 });
    CHECK(c.was == std::vector<uint8_t>{ 0xA9, 0x04 });
    QApplication::processEvents();
    e.editor->refresh();
    CHECK(e.lines()->item(0, 3)->text() == "now LDA #$01");

    e.lines()->selectRow(0);
    child<QPushButton>(*e.editor, "put_back")->click();
    REQUIRE(e.src.applied.size() == 2);
    CHECK_FALSE(e.src.applied[1].second);
    CHECK(e.src.now[0x19EF] == 0x04);

    // The same as the game has: nothing to apply.
    child<QPushButton>(*e.editor, "apply")->click();
    CHECK(e.src.applied.size() == 2);
    CHECK(message(*e.editor).contains("already"));
}

TEST_CASE("ADC # can swap to SBC with its number; a branch becomes its opposite, always or never; math swaps")
{
    Editing e;
    e.lines()->selectRow(1);
    auto swap = child<QCheckBox>(*e.editor, "swap_math");
    CHECK(swap->isVisible());
    CHECK(swap->text().contains("SBC"));
    swap->setChecked(true);
    child<QPushButton>(*e.editor, "apply")->click();
    CHECK(e.src.applied.back().first.value == std::vector<uint8_t>{ 0xE9, 0x01 });

    e.lines()->selectRow(2);
    auto branch = child<QComboBox>(*e.editor, "branch");
    REQUIRE(branch->count() == 3);
    branch->setCurrentIndex(0);
    child<QPushButton>(*e.editor, "apply")->click();
    CHECK(e.src.applied.back().first.value == std::vector<uint8_t>{ 0xD0 });
    branch->setCurrentIndex(2);
    child<QPushButton>(*e.editor, "apply")->click();
    CHECK(e.src.applied.back().first.value == std::vector<uint8_t>{ 0xEA, 0xEA });
    CHECK(e.src.applied.back().first.was == std::vector<uint8_t>{ 0xF0, 0x02 });

    e.lines()->selectRow(3);
    CHECK(child<QLabel>(*e.editor, "math")->text().contains("DEC"));
    child<QPushButton>(*e.editor, "apply")->click();
    CHECK(e.src.applied.back().first.kind == ChangeKind::Math);
    CHECK(e.src.applied.back().first.value == std::vector<uint8_t>{ 0xC6 });
}

TEST_CASE("a call: skipped, or sent to a near function in its bank (#35)")
{
    Editing e;
    e.lines()->selectRow(4);
    auto call = child<QComboBox>(*e.editor, "call");
    // Skip, then near functions in bank $80 other than itself and the one it calls already.
    REQUIRE(call->count() == 2);
    CHECK(call->itemText(1).contains("jump"));
    child<QPushButton>(*e.editor, "apply")->click();
    CHECK(e.src.applied.back().first.kind == ChangeKind::Skip);
    CHECK(e.src.applied.back().first.value == std::vector<uint8_t>(3, 0xEA));
    call->setCurrentIndex(1);
    child<QPushButton>(*e.editor, "apply")->click();
    CHECK(e.src.applied.back().first.kind == ChangeKind::Redirect);
    CHECK(e.src.applied.back().first.value == std::vector<uint8_t>{ 0x20, 0x00, 0xA1 });
}

TEST_CASE("a change is kept with its hotkey and start, shows as kept, and is removed again")
{
    Editing e;
    e.lines()->selectRow(3);
    child<QComboBox>(*e.editor, "hotkey")->setCurrentIndex(4);
    child<QCheckBox>(*e.editor, "start_on")->setChecked(false);
    auto forget = child<QPushButton>(*e.editor, "forget");
    CHECK_FALSE(forget->isEnabled());
    child<QPushButton>(*e.editor, "keep")->click();
    REQUIRE(e.src.kept_changes.size() == 1);
    CHECK(e.src.kept_changes[0].hotkey == 4);
    CHECK_FALSE(e.src.kept_changes[0].start_on);
    CHECK(e.src.kept_changes[0].note == "scroll (erik)");
    QApplication::processEvents();
    CHECK(e.lines()->item(3, 3)->text().contains("kept, key 4"));
    CHECK(forget->isEnabled());
    forget->click();
    CHECK(e.src.forgotten_changes == std::vector<uint32_t>{ 0x19F4 });
}

TEST_CASE("Change It with corruption off says so and changes nothing")
{
    Editing e;
    e.src.on = false;
    e.editor->refresh();
    CHECK(child<QWidget>(*e.editor, "not_running")->isVisible());
    e.lines()->selectRow(3);
    child<QPushButton>(*e.editor, "apply")->click();
    CHECK(e.src.applied.empty());
    CHECK(message(*e.editor).contains("corruption is off"));
}

TEST_CASE("Change It with nothing to change says so; nothing in it is unnamed for a screen reader")
{
    FakeFunctions src;
    FunctionEditor empty(src, src.rows[1]);
    empty.show();
    CHECK(child<QLabel>(empty, "empty")->isVisible());
    CHECK_FALSE(child<QTableWidget>(empty, "lines")->isVisible());

    Editing e;
    auto issues = ui::audit(e.editor.get());
    CAPTURE(issues.join("\n").toStdString());
    CHECK(issues.isEmpty());
}

TEST_CASE("a change kept with a timer; a timer longer than its cycle is refused")
{
    Editing e;
    e.lines()->selectRow(3);
    child<QCheckBox>(*e.editor, "timer")->setChecked(true);
    child<QDoubleSpinBox>(*e.editor, "timer_for")->setValue(1);
    child<QDoubleSpinBox>(*e.editor, "timer_every")->setValue(4);
    child<QPushButton>(*e.editor, "keep")->click();
    REQUIRE(e.src.kept_changes.size() == 1);
    CHECK(e.src.kept_changes[0].for_frames == 60);
    CHECK(e.src.kept_changes[0].every == 240);
    QApplication::processEvents();
    CHECK(e.lines()->item(3, 3)->text().contains("on 1 s every 4 s"));
    child<QDoubleSpinBox>(*e.editor, "timer_for")->setValue(5);
    child<QPushButton>(*e.editor, "keep")->click();
    CHECK(e.src.kept_changes.size() == 1);
    CHECK(message(*e.editor).contains("shorter"));
}
