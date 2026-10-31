#include "doctest/doctest.h"
#include "corrupt/recipe_json.h"
#include "corrupt/rule_text.h"
#include "RuleEditor.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

using namespace corrupt;

namespace
{

template <typename W>
W *child(RuleEditor &e, const char *name)
{
    auto w = e.findChild<W *>(name);
    REQUIRE_MESSAGE(w, name);
    return w;
}

void choose(RuleEditor &e, const char *combo, const QString &data)
{
    auto c = child<QComboBox>(e, combo);
    int i = c->findData(data);
    REQUIRE_MESSAGE(i >= 0, combo, " has no ", data.toStdString());
    c->setCurrentIndex(i);
}

bool ok_enabled(RuleEditor &e)
{
    return child<QDialogButtonBox>(e, "buttons")->button(QDialogButtonBox::Ok)->isEnabled();
}

// Every op and trigger type, with the optional bits set.
std::vector<Rule> sample_rules()
{
    std::vector<Rule> rules;
    auto base = [] {
        Rule r = default_rule();
        r.name = "sample";
        r.range = { Target::WRAM, 0x2000, 0x2FFF };
        return r;
    };

    Rule r = base();
    r.op = { .type = OpType::Add, .min = -3, .max = 5, .clamp = true };
    r.when = { .type = TriggerType::EveryFrames, .every = 30, .window = FrameWindow{ 60, 600 } };
    r.count = 4;
    r.pick = Pick::Sticky;
    r.intensity = 0.5;
    r.salt = 77;
    r.avoid_hot = true;
    rules.push_back(r);

    r = base();
    r.range = { Target::CGRAM, 0, 0x1FF };
    r.op = { .type = OpType::Random, .min = 16, .max = 31 };
    r.when = { .type = TriggerType::ChancePerFrame, .chance = 0.025 };
    rules.push_back(r);

    r = base();
    r.op = { .type = OpType::Bitflip, .bits = 3 };
    r.when = { .type = TriggerType::OnceAtFrame, .frame = 900 };
    r.enabled = false;
    rules.push_back(r);

    r = base();
    r.op = { .type = OpType::Bitflip, .mask = uint8_t(0x81) };
    r.when = { .type = TriggerType::Hotkey, .hotkey = 4 };
    rules.push_back(r);

    r = base();
    r.op = { .type = OpType::Swap, .width = 2, .a = 0x0F00u, .b = 0x0F02u };
    rules.push_back(r);

    r = base();
    r.op = { .type = OpType::Swap, .width = 1 };
    rules.push_back(r);

    r = base();
    r.op = { .type = OpType::Copy, .from = AddressRange{ Target::VRAM, 0x1000, 0x1FFF }, .length = 16 };
    rules.push_back(r);

    r = base();
    r.op = { .type = OpType::Freeze, .value = uint8_t(0x63) };
    rules.push_back(r);

    r = base();
    r.op = { .type = OpType::Freeze };
    rules.push_back(r);

    r = base();
    r.range = { Target::OAM, 0, 0x21F };
    r.op = { .type = OpType::Shuffle, .window = 12 };
    rules.push_back(r);

    r = base();
    r.range = { Target::WRAM, 0x0100, 0x01FF };
    r.region = "enemy_table";
    rules.push_back(r);

    // Rectangles and area ops.
    r = base();
    set_rect(r, Target::ROM, 0x1000, Rect{ 64, 16, 32 });
    r.op = { .type = OpType::Shift, .dx = 3, .dy = -2 };
    r.when = { .type = TriggerType::AtLoad };
    rules.push_back(r);

    r = base();
    set_rect(r, Target::WRAM, 0x2000, Rect{ 32, 8, 4 });
    r.op = { .type = OpType::Shift, .value = uint8_t(0x7F), .dx = -1, .wrap = false };
    rules.push_back(r);

    r = base();
    r.op = { .type = OpType::Flip, .axis = Axis::Vertical };
    rules.push_back(r);

    r = base();
    r.op = { .type = OpType::Levels, .curve = Curve::Posterize, .amount = 4 };
    rules.push_back(r);

    r = base();
    r.op = { .type = OpType::Levels, .curve = Curve::Gamma, .amount = 2.5 };
    rules.push_back(r);

    r = base();
    r.op = { .type = OpType::Levels, .curve = Curve::Invert };
    rules.push_back(r);

    r = base();
    r.op = { .type = OpType::Filter, .kernel = Kernel::Emboss };
    rules.push_back(r);

    r = base();
    set_rect(r, Target::WRAM, 0x2000, Rect{ 16, 4, 4 });
    r.op = { .type = OpType::Stamp, .source = 0x3000 };
    rules.push_back(r);

    r = base();
    r.op = { .type = OpType::Fill, .value = uint8_t(0xEA) };
    rules.push_back(r);

    // Aimed ROM rules and code ops.
    r = base();
    r.range = { Target::ROM, 0, 0xFFFF };
    r.op = { .type = OpType::Add, .min = -1, .max = 1 };
    r.aim = { .role = Role::Operand, .classes = { OpClass::Arith, OpClass::Compare } };
    rules.push_back(r);

    r = base();
    r.range = { Target::ROM, 0, 0xFFFF };
    r.op = { .type = OpType::OpcodeSwap };
    r.when = { .type = TriggerType::AtLoad };
    r.aim = { .except = { OpClass::Stack, OpClass::Width } }; // the editor lists them in class order
    rules.push_back(r);

    r = base();
    r.range = { Target::ROM, 0, 0xFFFF };
    r.op = { .type = OpType::BranchFlip };
    rules.push_back(r);

    r = base();
    r.range = { Target::ROM, 0x10000, 0x1FFFF };
    r.op = { .type = OpType::NopOut };
    r.aim = { .classes = { OpClass::Jump } };
    rules.push_back(r);

    r = base();
    r.range = { Target::ROM, 0, 0xFFFF };
    r.op = { .type = OpType::Random, .min = 0, .max = 255 };
    r.aim = { .role = Role::Data };
    rules.push_back(r);
    return rules;
}

Regions sample_regions()
{
    return { { "enemy_table", { Target::WRAM, 0x0100, 0x01FF } }, { "palette_shadow", { Target::WRAM, 0x0300, 0x04FF } } };
}

// Compare through the canonical JSON: it covers every saved field.
std::string as_json(const Rule &rule)
{
    Recipe r;
    r.name = "x";
    r.rules = { rule };
    return write_recipe(r);
}

void maybe_shot(RuleEditor &e, const char *name)
{
    if (auto dir = getenv("CORRUPT_SHOTS_DIR"))
    {
        QDir().mkpath(dir);
        QApplication::processEvents(); // the dialog fits itself after a change
        e.adjustSize();
        e.grab().save(QString("%1/%2.png").arg(dir, name));
    }
}

} // namespace

TEST_CASE("a rule goes through the editor unchanged, for every op and trigger")
{
    SizedMemory sizes;
    for (auto &rule : sample_rules())
    {
        RuleEditor e(rule, sample_regions(), sizes);
        CAPTURE(describe_rule(rule));
        CHECK(as_json(e.rule()) == as_json(rule));
        CHECK(e.rule().salt == rule.salt);
        CHECK(e.rule().avoid_hot == rule.avoid_hot);
        CHECK(e.error().isEmpty());
        CHECK(ok_enabled(e));
    }
}

TEST_CASE("aiming: only for the ROM, code ops aim at opcodes, and leaving out suggests width and stack")
{
    SizedMemory sizes;
    RuleEditor e(default_rule(), sample_regions(), sizes);
    auto role = child<QComboBox>(e, "role");
    auto mode = child<QComboBox>(e, "class_mode");
    choose(e, "target", "wram");
    CHECK(!role->isEnabled());
    CHECK(!mode->isEnabled());

    choose(e, "target", "rom");
    child<QLineEdit>(e, "first")->setText("0x0");
    child<QLineEdit>(e, "last")->setText("0xFFFF");
    CHECK(role->isEnabled());
    choose(e, "role", "operand");
    CHECK(e.rule().aim.role == Role::Operand);

    choose(e, "class_mode", "except");
    CHECK(child<QCheckBox>(e, "class_width")->isChecked()); // suggested, not forced
    CHECK(child<QCheckBox>(e, "class_stack")->isChecked());
    CHECK(e.rule().aim.except == std::vector<OpClass>{ OpClass::Stack, OpClass::Width });
    child<QCheckBox>(e, "class_stack")->setChecked(false);
    CHECK(e.rule().aim.except == std::vector<OpClass>{ OpClass::Width });

    choose(e, "class_mode", "only");
    for (auto c : e.findChildren<QCheckBox *>())
        if (c->objectName().startsWith("class_"))
            c->setChecked(false);
    CHECK(!ok_enabled(e)); // "only" with nothing ticked picks nothing
    child<QCheckBox>(e, "class_branch")->setChecked(true);
    CHECK(ok_enabled(e));
    CHECK(e.rule().aim.classes == std::vector<OpClass>{ OpClass::Branch });

    choose(e, "op", "opcode_swap");
    CHECK(!role->isEnabled()); // code ops always aim at opcodes
    CHECK(e.rule().aim.role == Role::Any);
    CHECK(ok_enabled(e));
    maybe_shot(e, "rule-editor-aim");

    choose(e, "target", "wram");
    CHECK(!e.rule().aim.active());
    CHECK(!ok_enabled(e)); // opcode_swap is ROM only
}

TEST_CASE("a rectangle is drawn from its corner, row width and size; area ops need no count")
{
    SizedMemory sizes;
    RuleEditor e(default_rule(), sample_regions(), sizes);
    choose(e, "target", "rom");
    child<QLineEdit>(e, "first")->setText("0x8000");
    choose(e, "shape", "rect");
    child<QSpinBox>(e, "rect_row")->setValue(64);
    child<QSpinBox>(e, "rect_width")->setValue(16);
    child<QSpinBox>(e, "rect_height")->setValue(8);
    CHECK_FALSE(child<QLineEdit>(e, "last")->isEnabled()); // follows from the rectangle
    choose(e, "op", "shift");
    child<QSpinBox>(e, "shift_dx")->setValue(5);
    CHECK_FALSE(child<QSpinBox>(e, "count")->isEnabled());
    auto r = e.rule();
    REQUIRE(r.rect);
    CHECK(r.range.first == 0x8000);
    CHECK(r.range.last == 0x8000 + 7 * 64 + 15);
    CHECK(r.rect->width == 16);
    CHECK(r.op.type == OpType::Shift);
    CHECK(r.op.dx == 5);
    CHECK(e.error().isEmpty());

    choose(e, "shape", "range"); // back to a plain range
    child<QLineEdit>(e, "last")->setText("0x80FF");
    CHECK_FALSE(e.rule().rect);
    choose(e, "op", "random");
    CHECK(child<QSpinBox>(e, "count")->isEnabled());
    maybe_shot(e, "rule-editor-rect");
}

TEST_CASE("editing fields builds the matching rule")
{
    SizedMemory sizes;
    RuleEditor e(default_rule(), sample_regions(), sizes);
    child<QLineEdit>(e, "name")->setText("palette chaos");
    choose(e, "target", "cgram");
    child<QLineEdit>(e, "first")->setText("0x20");
    child<QLineEdit>(e, "last")->setText("$3F");
    choose(e, "op", "add");
    child<QSpinBox>(e, "add_min")->setValue(-4);
    child<QSpinBox>(e, "add_max")->setValue(4);
    choose(e, "when", "chance_per_frame");
    child<QDoubleSpinBox>(e, "chance")->setValue(5.0); // percent
    child<QSpinBox>(e, "count")->setValue(3);

    Rule r = e.rule();
    CHECK(r.name == "palette chaos");
    CHECK(r.range.target == Target::CGRAM);
    CHECK(r.range.first == 0x20u);
    CHECK(r.range.last == 0x3Fu);
    CHECK(r.region.empty());
    CHECK(r.op.type == OpType::Add);
    CHECK(r.op.min == -4);
    CHECK(r.op.max == 4);
    CHECK(r.when.type == TriggerType::ChancePerFrame);
    CHECK(r.when.chance == doctest::Approx(0.05));
    CHECK(r.count == 3u);
    CHECK(ok_enabled(e));
    CHECK(child<QLabel>(e, "summary")->text().toStdString() == describe_rule(r));
    maybe_shot(e, "rule-editor-add");
}

TEST_CASE("an address outside the target disables OK and says why")
{
    SizedMemory sizes;
    RuleEditor e(default_rule(), {}, sizes);
    choose(e, "target", "oam");
    child<QLineEdit>(e, "first")->setText("0");
    child<QLineEdit>(e, "last")->setText("0x300");
    CHECK_FALSE(ok_enabled(e));
    auto text = child<QLabel>(e, "error")->text();
    CHECK(text.contains("oam"));
    CHECK(text.contains("0x220")); // the target's size
    maybe_shot(e, "rule-editor-error");

    child<QLineEdit>(e, "last")->setText("0x21F");
    CHECK(ok_enabled(e));
    CHECK(child<QLabel>(e, "error")->text().isEmpty());
}

TEST_CASE("text that isn't a number is an error, not a zero")
{
    SizedMemory sizes;
    RuleEditor e(default_rule(), {}, sizes);
    child<QLineEdit>(e, "first")->setText("0x2g00");
    CHECK_FALSE(ok_enabled(e));
    CHECK(child<QLabel>(e, "error")->text().contains("from"));
}

TEST_CASE("a game without save RAM can't take an sram rule")
{
    SizedMemory sizes;
    sizes.sizes[size_t(Target::SRAM)] = 0;
    RuleEditor e(default_rule(), {}, sizes);
    choose(e, "target", "sram");
    child<QLineEdit>(e, "first")->setText("0");
    child<QLineEdit>(e, "last")->setText("0xFF");
    CHECK_FALSE(ok_enabled(e));
}

TEST_CASE("a named region fills the range and is kept for saving")
{
    SizedMemory sizes;
    RuleEditor e(default_rule(), sample_regions(), sizes);
    choose(e, "region", "palette_shadow");
    Rule r = e.rule();
    CHECK(r.region == "palette_shadow");
    CHECK(r.range.target == Target::WRAM);
    CHECK(r.range.first == 0x300u);
    CHECK(r.range.last == 0x4FFu);
    CHECK_FALSE(child<QLineEdit>(e, "first")->isEnabled());
    CHECK(child<QLineEdit>(e, "first")->text() == "0x0300");

    choose(e, "region", "");
    CHECK(e.rule().region.empty());
    CHECK(child<QLineEdit>(e, "first")->isEnabled());
    CHECK(e.rule().range.first == 0x300u); // the range stays as a starting point
}

TEST_CASE("op-specific choices: fixed swap, xor mask, freeze value, copy source, window")
{
    SizedMemory sizes;
    RuleEditor e(default_rule(), {}, sizes);

    choose(e, "op", "swap");
    choose(e, "swap_mode", "fixed");
    child<QLineEdit>(e, "swap_a")->setText("0x0F00");
    child<QLineEdit>(e, "swap_b")->setText("0x0F02");
    choose(e, "swap_width", "2");
    Rule r = e.rule();
    REQUIRE(r.op.a);
    REQUIRE(r.op.b);
    CHECK(*r.op.a == 0x0F00u);
    CHECK(*r.op.b == 0x0F02u);
    CHECK(r.op.width == 2);
    maybe_shot(e, "rule-editor-swap");

    choose(e, "op", "bitflip");
    choose(e, "bitflip_mode", "mask");
    child<QLineEdit>(e, "bitflip_mask")->setText("0x81");
    r = e.rule();
    REQUIRE(r.op.mask);
    CHECK(*r.op.mask == 0x81);

    choose(e, "op", "freeze");
    choose(e, "freeze_mode", "value");
    child<QLineEdit>(e, "freeze_value")->setText("99");
    r = e.rule();
    REQUIRE(r.op.value);
    CHECK(*r.op.value == 99);
    child<QLineEdit>(e, "freeze_value")->setText("300");
    CHECK_FALSE(ok_enabled(e));

    choose(e, "op", "copy");
    choose(e, "copy_target", "vram");
    child<QLineEdit>(e, "copy_first")->setText("0x1000");
    child<QLineEdit>(e, "copy_last")->setText("0x10FF");
    child<QSpinBox>(e, "copy_length")->setValue(32);
    r = e.rule();
    REQUIRE(r.op.from);
    CHECK(r.op.from->target == Target::VRAM);
    CHECK(r.op.from->last == 0x10FFu);
    CHECK(r.op.length == 32u);
    CHECK(ok_enabled(e));

    child<QCheckBox>(e, "window_enabled")->setChecked(true);
    child<QSpinBox>(e, "window_start")->setValue(600);
    child<QSpinBox>(e, "window_end")->setValue(100);
    CHECK_FALSE(ok_enabled(e)); // start after end
    child<QSpinBox>(e, "window_end")->setValue(1200);
    r = e.rule();
    REQUIRE(r.when.window);
    CHECK(r.when.window->start == 600u);
    CHECK(r.when.window->end == 1200u);
    CHECK(ok_enabled(e));
}

TEST_CASE("an empty name is not allowed")
{
    SizedMemory sizes;
    RuleEditor e(default_rule(), {}, sizes);
    child<QLineEdit>(e, "name")->setText("  ");
    CHECK_FALSE(ok_enabled(e));
    CHECK(child<QLabel>(e, "error")->text().contains("name"));
}

TEST_CASE("avoid dangerous bytes is a checkbox, kept both ways")
{
    SizedMemory sizes;
    Rule r = default_rule();
    RuleEditor e(r, sample_regions(), sizes);
    auto avoid = child<QCheckBox>(e, "avoid_hot");
    CHECK_FALSE(avoid->isChecked());
    CHECK_FALSE(avoid->toolTip().isEmpty());
    avoid->setChecked(true);
    CHECK(e.rule().avoid_hot);

    r.avoid_hot = true;
    RuleEditor again(r, sample_regions(), sizes);
    CHECK(child<QCheckBox>(again, "avoid_hot")->isChecked());
    child<QCheckBox>(again, "avoid_hot")->setChecked(false);
    CHECK_FALSE(again.rule().avoid_hot);
    maybe_shot(again, "rule-editor-avoid");
}
