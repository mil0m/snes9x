#include "RuleEditor.hpp"
#include "corrupt/rule_text.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <climits>

using namespace corrupt;

namespace
{

constexpr OpType kOps[] = { OpType::Random, OpType::Add,   OpType::Bitflip, OpType::Swap,   OpType::Copy,
                            OpType::Freeze, OpType::Shuffle, OpType::Shift,  OpType::Flip,   OpType::Levels,
                            OpType::Filter, OpType::Stamp,   OpType::Fill,    OpType::OpcodeSwap,
                            OpType::BranchFlip, OpType::NopOut };
constexpr TriggerType kTriggers[] = { TriggerType::EveryFrames, TriggerType::ChancePerFrame,
                                      TriggerType::OnceAtFrame, TriggerType::Hotkey, TriggerType::AtLoad };

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

QString hex4(uint32_t v)
{
    return QString("0x%1").arg(v, 4, 16, QChar('0')).toUpper().replace("0X", "0x");
}

QString hex2(uint8_t v)
{
    return QString("0x%1").arg(v, 2, 16, QChar('0')).toUpper().replace("0X", "0x");
}

QString op_label(OpType t)
{
    switch (t)
    {
    case OpType::Random: return QObject::tr("Random value");
    case OpType::Add: return QObject::tr("Add / subtract");
    case OpType::Bitflip: return QObject::tr("Flip bits");
    case OpType::Swap: return QObject::tr("Swap bytes");
    case OpType::Copy: return QObject::tr("Copy a block");
    case OpType::Freeze: return QObject::tr("Freeze");
    case OpType::Shuffle: return QObject::tr("Shuffle a window");
    case OpType::Shift: return QObject::tr("Shift the whole area");
    case OpType::Flip: return QObject::tr("Flip the whole area");
    case OpType::Levels: return QObject::tr("Levels: remap values");
    case OpType::Filter: return QObject::tr("Filter (blur, sharpen…)");
    case OpType::Stamp: return QObject::tr("Stamp another area onto it");
    case OpType::Fill: return QObject::tr("Fill with a value");
    case OpType::OpcodeSwap: return QObject::tr("Swap opcodes (same length, ROM)");
    case OpType::BranchFlip: return QObject::tr("Flip branches (ROM)");
    case OpType::NopOut: return QObject::tr("NOP out instructions (ROM)");
    }
    return {};
}

QString trigger_label(TriggerType t)
{
    switch (t)
    {
    case TriggerType::EveryFrames: return QObject::tr("Every N frames");
    case TriggerType::ChancePerFrame: return QObject::tr("Chance per frame");
    case TriggerType::OnceAtFrame: return QObject::tr("Once, at a frame");
    case TriggerType::Hotkey: return QObject::tr("Hotkey");
    case TriggerType::AtLoad: return QObject::tr("Once, when the run starts (classic ROM)");
    }
    return {};
}

QComboBox *combo(QWidget *parent, const char *name)
{
    auto c = new QComboBox(parent);
    c->setObjectName(name);
    return c;
}

QSpinBox *spin(QWidget *parent, const char *name, int lo, int hi)
{
    auto s = new QSpinBox(parent);
    s->setObjectName(name);
    s->setRange(lo, hi);
    return s;
}

QLineEdit *line(QWidget *parent, const char *name, const QString &placeholder = {})
{
    auto e = new QLineEdit(parent);
    e->setObjectName(name);
    e->setPlaceholderText(placeholder);
    return e;
}

} // namespace

QString targetLabel(Target t)
{
    switch (t)
    {
    case Target::WRAM: return QObject::tr("wram (work RAM)");
    case Target::SRAM: return QObject::tr("sram (save RAM)");
    case Target::VRAM: return QObject::tr("vram (graphics)");
    case Target::OAM: return QObject::tr("oam (sprites)");
    case Target::CGRAM: return QObject::tr("cgram (palette)");
    case Target::ARAM: return QObject::tr("aram (sound)");
    case Target::ROM: return QObject::tr("rom (the game)");
    }
    return {};
}

namespace
{

void fill_targets(QComboBox *c)
{
    for (auto t : kAllTargets)
        c->addItem(targetLabel(t), QString(target_name(t)));
}

// Shows "5 %" rather than "5.0000 %", but keeps four decimals for values like 0.0625.
class CompactSpinBox : public QDoubleSpinBox
{
  public:
    using QDoubleSpinBox::QDoubleSpinBox;
    QString textFromValue(double value) const override
    {
        return locale().toString(value, 'g', 8);
    }
};

// Helper text: quieter than the controls, but not greyed out like something disabled.
QLabel *hint_label(const QString &text, QWidget *parent)
{
    auto label = new QLabel(text, parent);
    label->setWordWrap(true);
    auto pal = label->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::PlaceholderText));
    label->setPalette(pal);
    return label;
}

void select(QComboBox *c, const QString &data)
{
    int i = c->findData(data);
    if (i >= 0)
        c->setCurrentIndex(i);
}

Target target_of(const QComboBox *c)
{
    return parse_target(c->currentData().toString().toStdString()).value_or(Target::WRAM);
}

// Parses a field; on failure records "<what>: ..." in `error` (first error wins).
std::optional<int64_t> number(const QLineEdit *e, const char *what, int64_t lo, int64_t hi, QString &error)
{
    auto n = parse_number(e->text().toStdString());
    if (!n)
    {
        if (error.isEmpty())
            error = QObject::tr("%1: \"%2\" is not a number").arg(what, e->text().trimmed());
        return std::nullopt;
    }
    if (*n < lo || *n > hi)
    {
        if (error.isEmpty())
            error = QObject::tr("%1: must be within %2..%3").arg(what).arg(lo).arg(hi);
        return std::nullopt;
    }
    return n;
}

} // namespace

RuleEditor::RuleEditor(const Rule &rule, const Regions &regions, const MemoryAccess &sizes, QWidget *parent)
    : QDialog(parent), original(rule), regions(regions), sizes(sizes)
{
    setWindowTitle(tr("Edit Rule"));
    buildForm();
    load(rule);
    refresh();
}

void RuleEditor::buildForm()
{
    // The sections scroll when the screen is short, instead of squeezing; the summary and
    // the buttons stay put below them.
    auto outer = new QVBoxLayout(this);
    scroll = new QScrollArea(this);
    scroll->setObjectName("sections");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sections = new QWidget(scroll);
    auto layout = new QVBoxLayout(sections);
    layout->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(sections);
    outer->addWidget(scroll, 1);

    auto top = new QFormLayout;
    name_edit = line(this, "name");
    name_edit->setToolTip(tr("What the rule is called in the panel's list"));
    top->addRow(tr("Name"), name_edit);
    layout->addLayout(top);

    // Where
    auto where_box = new QGroupBox(tr("Where"), this);
    auto where = new QFormLayout(where_box);
    target_combo = combo(where_box, "target");
    fill_targets(target_combo);
    target_combo->setToolTip(tr("Which of the console's memories to change: wram is the game's working memory "
                                "(positions, health, timers), vram graphics, cgram colours, oam sprites, aram "
                                "sound, sram the battery save, rom the game itself"));
    where->addRow(tr("Memory"), target_combo);
    where_form = where;
    region_combo = combo(where_box, "region");
    region_combo->addItem(tr("Custom range"), QString());
    for (auto &[name, range] : regions)
        region_combo->addItem(QString("%1 (%2)").arg(qs(name), target_name(range.target)), qs(name));
    region_combo->setVisible(!regions.empty());
    if (!regions.empty())
        where->addRow(tr("Region"), region_combo);
    auto range_row = new QHBoxLayout;
    first_edit = line(where_box, "first", "0x0000");
    last_edit = line(where_box, "last", "0xFFFF");
    range_row->addWidget(first_edit);
    range_row->addWidget(new QLabel(tr("to"), where_box));
    range_row->addWidget(last_edit);
    first_edit->setToolTip(tr("The first byte, as an offset into the memory (0x1A00, or a plain number)"));
    last_edit->setToolTip(tr("The last byte (included)"));
    where->addRow(tr("From"), range_row);
    shape_combo = combo(where_box, "shape");
    shape_combo->addItem(tr("A range of bytes"), QString("range"));
    shape_combo->addItem(tr("A rectangle (as drawn in the pixel view)"), QString("rect"));
    shape_combo->setToolTip(tr("A rectangle comes from selecting with Alt-drag in the memory viewer's pixel view"));
    where->addRow(tr("Shape"), shape_combo);
    rect_fields = new QWidget(where_box);
    auto rect_layout = new QHBoxLayout(rect_fields);
    rect_layout->setContentsMargins(0, 0, 0, 0);
    rect_width = spin(rect_fields, "rect_width", 1, 0xFFFFFF);
    rect_height = spin(rect_fields, "rect_height", 1, 0xFFFFFF);
    rect_row = spin(rect_fields, "rect_row", 1, 0xFFFFFF);
    rect_layout->addWidget(rect_width);
    rect_layout->addWidget(new QLabel(QString::fromUtf8("×"), rect_fields));
    rect_layout->addWidget(rect_height);
    rect_layout->addWidget(new QLabel(tr("in rows of"), rect_fields));
    rect_layout->addWidget(rect_row);
    rect_fields->setToolTip(tr("Width and height in bytes and rows, from the From byte as its top-left corner. "
                               "Rows of: the pixel view's row width it was drawn in."));
    where->addRow(tr("Rectangle"), rect_fields);
    no_aim_hint = hint_label(tr("To pick bytes by what they are (code, data, graphics), choose rom (the game)."), where_box);
    no_aim_hint->setObjectName("no_aim_hint");
    where->addRow(no_aim_hint);
    layout->addWidget(where_box);

    // What
    auto what_box = new QGroupBox(tr("What"), this);
    auto what = new QVBoxLayout(what_box);
    op_combo = combo(what_box, "op");
    for (auto t : kOps)
        op_combo->addItem(op_label(t), QString(op_name(t)));
    op_combo->setToolTip(tr("What to do to each picked byte"));
    what->addWidget(op_combo);
    op_pages = new QStackedWidget(what_box);
    op_pages->setObjectName("op_pages");
    what->addWidget(op_pages);
    auto page = [this](QFormLayout *&form) {
        auto w = new QWidget(op_pages);
        form = new QFormLayout(w);
        form->setContentsMargins(0, 0, 0, 0);
        op_pages->addWidget(w);
        return w;
    };
    QFormLayout *f;

    auto random_page = page(f);
    random_min = spin(random_page, "random_min", 0, 255);
    random_max = spin(random_page, "random_max", 0, 255);
    random_min->setToolTip(tr("Each byte becomes a random value from Lowest to Highest (0-255)"));
    random_max->setToolTip(random_min->toolTip());
    f->addRow(tr("Lowest"), random_min);
    f->addRow(tr("Highest"), random_max);

    auto add_page = page(f);
    add_min = spin(add_page, "add_min", -255, 255);
    add_max = spin(add_page, "add_max", -255, 255);
    add_clamp = new QCheckBox(tr("Clamp at 0 and 255 instead of wrapping"), add_page);
    add_clamp->setObjectName("add_clamp");
    add_min->setToolTip(tr("Each byte goes up or down by a random amount in this range: -2 to 2 is a nudge"));
    add_max->setToolTip(add_min->toolTip());
    f->addRow(tr("Smallest change"), add_min);
    f->addRow(tr("Largest change"), add_max);
    f->addRow(add_clamp);

    auto bitflip_page = page(f);
    bitflip_mode = combo(bitflip_page, "bitflip_mode");
    bitflip_mode->addItem(tr("Random bits"), QString("bits"));
    bitflip_mode->addItem(tr("XOR with a mask"), QString("mask"));
    bitflip_bits = spin(bitflip_page, "bitflip_bits", 1, 8);
    bitflip_mask = line(bitflip_page, "bitflip_mask", "0x81");
    bitflip_bits->setToolTip(tr("How many of the byte's 8 bits to flip, picked at random"));
    bitflip_mask->setToolTip(tr("Flip exactly these bits (0x81 flips the top and bottom one)"));
    f->addRow(tr("Mode"), bitflip_mode);
    f->addRow(tr("Bits to flip"), bitflip_bits);
    f->addRow(tr("Mask"), bitflip_mask);

    auto swap_page = page(f);
    swap_mode = combo(swap_page, "swap_mode");
    swap_mode->addItem(tr("Each hit with a random address"), QString("random"));
    swap_mode->addItem(tr("A fixed pair"), QString("fixed"));
    swap_a = line(swap_page, "swap_a", "0x0F00");
    swap_b = line(swap_page, "swap_b", "0x0F02");
    swap_width = combo(swap_page, "swap_width");
    swap_width->addItem(tr("8-bit"), QString("1"));
    swap_width->addItem(tr("16-bit"), QString("2"));
    swap_mode->setToolTip(tr("Trade each picked byte with another one in the range, or always the same two"));
    f->addRow(tr("Mode"), swap_mode);
    f->addRow(tr("Address A"), swap_a);
    f->addRow(tr("Address B"), swap_b);
    f->addRow(tr("Width"), swap_width);

    auto copy_page = page(f);
    copy_target = combo(copy_page, "copy_target");
    fill_targets(copy_target);
    auto copy_row = new QHBoxLayout;
    copy_first = line(copy_page, "copy_first", "0x0000");
    copy_last = line(copy_page, "copy_last", "0xFFFF");
    copy_row->addWidget(copy_first);
    copy_row->addWidget(new QLabel(tr("to"), copy_page));
    copy_row->addWidget(copy_last);
    copy_length = spin(copy_page, "copy_length", 1, 65536);
    copy_target->setToolTip(tr("Copies a block from somewhere (another memory too) over each picked byte"));
    f->addRow(tr("Copy from"), copy_target);
    f->addRow(tr("Source range"), copy_row);
    f->addRow(tr("Bytes per hit"), copy_length);

    auto freeze_page = page(f);
    freeze_mode = combo(freeze_page, "freeze_mode");
    freeze_mode->addItem(tr("Hold whatever it becomes when hit"), QString("hit"));
    freeze_mode->addItem(tr("Hold a value"), QString("value"));
    freeze_value = line(freeze_page, "freeze_value", "0x00");
    freeze_mode->setToolTip(tr("Keep the byte from changing: infinite health, a stuck timer"));
    f->addRow(tr("Mode"), freeze_mode);
    f->addRow(tr("Value"), freeze_value);

    auto shuffle_page = page(f);
    shuffle_window = spin(shuffle_page, "shuffle_window", 2, 256);
    shuffle_window->setToolTip(tr("Mixes up the order of this many bytes from each picked one on"));
    f->addRow(tr("Window (bytes)"), shuffle_window);

    auto shift_page = page(f);
    shift_dx = spin(shift_page, "shift_dx", -0xFFFF, 0xFFFF);
    shift_dy = spin(shift_page, "shift_dy", -0xFFFF, 0xFFFF);
    shift_wrap = new QCheckBox(tr("Wrap around (what leaves one side comes back on the other)"), shift_page);
    shift_wrap->setObjectName("shift_wrap");
    shift_fill = line(shift_page, "shift_fill", "0x00");
    f->addRow(tr("Right by (bytes)"), shift_dx);
    f->addRow(tr("Down by (rows)"), shift_dy);
    f->addRow(shift_wrap);
    f->addRow(tr("Fill with"), shift_fill);

    auto flip_page = page(f);
    flip_axis = combo(flip_page, "flip_axis");
    flip_axis->addItem(tr("Horizontally (each row reversed)"), QString(axis_name(Axis::Horizontal)));
    flip_axis->addItem(tr("Vertically (the rows upside down)"), QString(axis_name(Axis::Vertical)));
    flip_axis->addItem(tr("Both"), QString(axis_name(Axis::Both)));
    f->addRow(tr("Flip"), flip_axis);

    auto levels_page = page(f);
    levels_curve = combo(levels_page, "levels_curve");
    levels_curve->addItem(tr("Invert"), QString(curve_name(Curve::Invert)));
    levels_curve->addItem(tr("Posterize (levels)"), QString(curve_name(Curve::Posterize)));
    levels_curve->addItem(tr("Threshold (from)"), QString(curve_name(Curve::Threshold)));
    levels_curve->addItem(tr("Gamma (above 1 brightens)"), QString(curve_name(Curve::Gamma)));
    levels_curve->addItem(tr("Contrast (-1 to 1)"), QString(curve_name(Curve::Contrast)));
    levels_amount = new QDoubleSpinBox(levels_page);
    levels_amount->setObjectName("levels_amount");
    levels_amount->setRange(-1.0, 255.0);
    levels_amount->setDecimals(2);
    f->addRow(tr("Curve"), levels_curve);
    f->addRow(tr("Amount"), levels_amount);

    auto filter_page = page(f);
    filter_kernel = combo(filter_page, "filter_kernel");
    for (auto k : { Kernel::Blur, Kernel::Sharpen, Kernel::Median, Kernel::Emboss, Kernel::Edges, Kernel::Max, Kernel::Min })
        filter_kernel->addItem(QString(kernel_name(k)), QString(kernel_name(k)));
    f->addRow(tr("Filter"), filter_kernel);

    auto stamp_page = page(f);
    stamp_from = line(stamp_page, "stamp_from", "0x0000");
    f->addRow(tr("Copy from (top-left)"), stamp_from);

    auto fill_page = page(f);
    fill_value = line(fill_page, "fill_value", "0x00");
    f->addRow(tr("Value"), fill_value);

    for (auto text : { tr("Each picked instruction becomes another of the same length and addressing mode: "
                          "ADC becomes SBC or AND, BNE becomes BEQ, INC becomes DEC."),
                       tr("Each picked conditional branch goes the other way: BEQ becomes BNE, BCC becomes BCS."),
                       tr("Each picked instruction becomes NOPs: a call, a store or a check that no longer "
                          "happens.") })
    {
        auto code_page = page(f);
        auto hint = new QLabel(text, code_page);
        hint->setWordWrap(true);
        f->addRow(hint);
    }
    layout->addWidget(what_box);

    // Aim (ROM only): which bytes, by what the code map knows about them.
    auto aim_group = new QGroupBox(tr("Aim (ROM, from the code map)"), this);
    aim_box = aim_group;
    auto aim = new QFormLayout(aim_group);
    role_combo = combo(aim_group, "role");
    role_combo->addItem(tr("Any byte in the range"), QString(role_name(Role::Any)));
    role_combo->addItem(tr("Opcodes: which instruction"), QString(role_name(Role::Opcode)));
    role_combo->addItem(tr("Operands: the values and addresses they use"), QString(role_name(Role::Operand)));
    role_combo->addItem(tr("Code: opcodes and operands"), QString(role_name(Role::Code)));
    role_combo->addItem(tr("Data the game reads"), QString(role_name(Role::Data)));
    role_combo->addItem(tr("Graphics it copies by DMA"), QString(role_name(Role::Dma)));
    role_combo->addItem(tr("Anything the game used"), QString(role_name(Role::Used)));
    aim->addRow(tr("Bytes"), role_combo);
    class_mode = combo(aim_group, "class_mode");
    class_mode->addItem(tr("Any instruction"), QString("all"));
    class_mode->addItem(tr("Only these instructions"), QString("only"));
    class_mode->addItem(tr("All but these instructions"), QString("except"));
    aim->addRow(tr("Instructions"), class_mode);
    auto grid = new QGridLayout;
    for (int i = 0; i < kOpClassCount; i++)
    {
        auto c = OpClass(i);
        auto check = new QCheckBox(class_name(c), aim_group);
        check->setObjectName(QString("class_") + class_name(c));
        static const char *about[] = { "ADC SBC INC DEC: arithmetic", "AND ORA EOR BIT: logic", "ASL LSR ROL ROR: shifts",
                                       "CMP CPX CPY: comparisons (the ifs' questions)", "LDA LDX LDY: loads",
                                       "STA STX STY STZ: stores", "TAX TXA…: moving values between registers",
                                       "BNE BEQ… BRA: branches (the ifs)", "JMP JSR JSL: jumps and calls",
                                       "RTS RTL RTI: returns", "PHA PLA…: the stack (crashes fast)",
                                       "CLC SEC…: flags", "REP SEP XCE: switch 8/16-bit width (crashes at once)",
                                       "MVN MVP: block moves", "NOP BRK WAI STP and others" };
        check->setToolTip(about[i]);
        grid->addWidget(check, i / 5, i % 5);
        class_checks.push_back({ c, check });
    }
    aim->addRow(grid);
    auto aim_hint = new QLabel(tr("Only bytes the code map knows: play first, and the map grows. Width (REP, SEP, "
                                  "XCE) and stack instructions almost always crash at once."),
                               aim_group);
    aim_hint->setWordWrap(true);
    auto pal = aim_hint->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::PlaceholderText));
    aim_hint->setPalette(pal);
    aim->addRow(aim_hint);
    layout->addWidget(aim_group);
    // Leaving instructions out starts from the ones that almost always crash.
    connect(class_mode, &QComboBox::currentIndexChanged, this, [this] {
        if (class_mode->currentData() != "except")
            return;
        for (auto [c, check] : class_checks)
            if (check->isChecked())
                return;
        for (auto [c, check] : class_checks)
            check->setChecked(c == OpClass::Width || c == OpClass::Stack);
    });

    // When
    auto when_box = new QGroupBox(tr("When"), this);
    auto when = new QVBoxLayout(when_box);
    when_combo = combo(when_box, "when");
    for (auto t : kTriggers)
        when_combo->addItem(trigger_label(t), QString(trigger_name(t)));
    when_combo->setToolTip(tr("When the rule fires. 60 frames are about a second"));
    when->addWidget(when_combo);
    when_pages = new QStackedWidget(when_box);
    when->addWidget(when_pages);
    auto when_page = [this](const QString &label, QWidget *field) {
        auto w = new QWidget(when_pages);
        auto form = new QFormLayout(w);
        form->setContentsMargins(0, 0, 0, 0);
        field->setParent(w);
        form->addRow(label, field);
        when_pages->addWidget(w);
    };
    every_frames = spin(when_box, "every", 1, INT_MAX);
    every_frames->setSuffix(tr(" frames"));
    when_page(tr("Every"), every_frames);
    chance = new CompactSpinBox(when_box);
    chance->setObjectName("chance");
    chance->setRange(0.0, 100.0);
    chance->setDecimals(4);
    chance->setSuffix(" %");
    when_page(tr("Chance"), chance);
    once_frame = spin(when_box, "once_frame", 1, INT_MAX);
    when_page(tr("At frame"), once_frame);
    hotkey = spin(when_box, "hotkey", 1, 9);
    hotkey->setToolTip(tr("Fires when you press corruption hotkey 1-9 (set them in Options > Shortcuts; 1 is F9)"));
    when_page(tr("Hotkey"), hotkey);
    auto at_load_note = new QLabel(tr("Fires once as the run starts. On the ROM, the console then restarts, "
                                      "so the game boots from the corrupted ROM."),
                                   when_box);
    at_load_note->setWordWrap(true);
    when_page({}, at_load_note);

    auto window_row = new QHBoxLayout;
    window_enabled = new QCheckBox(tr("Only from frame"), when_box);
    window_enabled->setObjectName("window_enabled");
    window_start = spin(when_box, "window_start", 0, INT_MAX);
    window_end = spin(when_box, "window_end", 0, INT_MAX);
    window_row->addWidget(window_enabled);
    window_row->addWidget(window_start, 1);
    window_row->addWidget(new QLabel(tr("to"), when_box));
    window_row->addWidget(window_end, 1);
    when->addLayout(window_row);
    auto frames_hint = hint_label(tr("Frames count from turning the recipe on; 60 is about a second."), when_box);
    window_enabled->setToolTip(tr("Only fire between these two frames of the run"));
    when->addWidget(frames_hint);
    layout->addWidget(when_box);

    // How much
    auto how_box = new QGroupBox(tr("How Much"), this);
    auto how = new QFormLayout(how_box);
    count_spin = spin(how_box, "count", 1, 1000000);
    count_spin->setToolTip(tr("How many bytes it changes each time it fires"));
    how->addRow(tr("Bytes each time"), count_spin);
    pick_combo = combo(how_box, "pick");
    pick_combo->addItem(tr("Different ones each time"), QString("fresh"));
    pick_combo->addItem(tr("The same ones every time"), QString("sticky"));
    pick_combo->setToolTip(tr("Pick new bytes on every firing, or pick once and keep hitting those"));
    how->addRow(tr("Which bytes"), pick_combo);
    intensity_spin = new CompactSpinBox(how_box);
    intensity_spin->setObjectName("intensity");
    intensity_spin->setRange(0.0, 100.0);
    intensity_spin->setDecimals(2);
    intensity_spin->setSingleStep(5);
    intensity_spin->setSuffix(" %");
    intensity_spin->setToolTip(tr("How hard this rule hits, on top of the recipe's Chaos. Below one byte each "
                                  "time, it fires less often instead"));
    how->addRow(tr("Strength"), intensity_spin);
    enabled_check = new QCheckBox(tr("Enabled"), how_box);
    enabled_check->setObjectName("enabled");
    enabled_check->setToolTip(tr("Off: the rule stays in the recipe but does nothing"));
    how->addRow(enabled_check);
    avoid_check = new QCheckBox(tr("Avoid dangerous bytes"), how_box);
    avoid_check->setObjectName("avoid_hot");
    avoid_check->setToolTip(tr("Skip bytes the crash map has learned crash this game (the memory viewer's "
                               "Danger overlay). Fewer crashes, longer runs; the map grows as you play."));
    how->addRow(avoid_check);
    layout->addWidget(how_box);

    layout->addStretch(1);
    summary_label = new QLabel(this);
    summary_label->setObjectName("summary");
    summary_label->setWordWrap(true);
    outer->addWidget(summary_label);
    error_label = new QLabel(this);
    error_label->setObjectName("error");
    error_label->setWordWrap(true);
    error_label->setStyleSheet("color: #d33;");
    outer->addWidget(error_label);

    buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName("buttons");
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Every change re-validates.
    auto changed = [this] { refresh(); };
    for (auto e : findChildren<QLineEdit *>())
        connect(e, &QLineEdit::textChanged, this, changed);
    for (auto s : findChildren<QSpinBox *>())
        connect(s, &QSpinBox::valueChanged, this, changed);
    for (auto s : findChildren<QDoubleSpinBox *>())
        connect(s, &QDoubleSpinBox::valueChanged, this, changed);
    for (auto c : findChildren<QCheckBox *>())
        connect(c, &QCheckBox::toggled, this, changed);
    for (auto c : findChildren<QComboBox *>())
        if (c != region_combo)
            connect(c, &QComboBox::currentIndexChanged, this, changed);
    connect(region_combo, &QComboBox::currentIndexChanged, this, [this] { regionChosen(); refresh(); });
}

void RuleEditor::load(const Rule &r)
{
    name_edit->setText(qs(r.name));
    select(target_combo, target_name(r.range.target));
    first_edit->setText(hex4(r.range.first));
    last_edit->setText(hex4(r.range.last));
    select(shape_combo, r.rect ? "rect" : "range");
    rect_row->setValue(r.rect ? int(r.rect->row) : 64);
    rect_width->setValue(r.rect ? int(r.rect->width) : 16);
    rect_height->setValue(r.rect ? int(r.rect->height) : 16);

    // Every op page gets a sensible start, so switching op shows usable values.
    random_min->setValue(0);
    random_max->setValue(255);
    add_min->setValue(-2);
    add_max->setValue(2);
    bitflip_bits->setValue(1);
    bitflip_mask->setText("0xFF");
    swap_a->setText(hex4(r.range.first));
    swap_b->setText(hex4(r.range.first + 1));
    select(copy_target, target_name(r.range.target));
    copy_first->setText(hex4(r.range.first));
    copy_last->setText(hex4(r.range.last));
    copy_length->setValue(1);
    freeze_value->setText("0x00");
    shuffle_window->setValue(8);
    shift_dx->setValue(1);
    shift_dy->setValue(0);
    shift_wrap->setChecked(true);
    shift_fill->setText("0x00");
    levels_amount->setValue(4);
    stamp_from->setText(hex4(r.range.first));
    fill_value->setText("0x00");

    const Op &op = r.op;
    select(op_combo, op_name(op.type));
    switch (op.type)
    {
    case OpType::Random:
        random_min->setValue(op.min);
        random_max->setValue(op.max);
        break;
    case OpType::Add:
        add_min->setValue(op.min);
        add_max->setValue(op.max);
        add_clamp->setChecked(op.clamp);
        break;
    case OpType::Bitflip:
        select(bitflip_mode, op.mask ? "mask" : "bits");
        bitflip_bits->setValue(op.bits);
        if (op.mask)
            bitflip_mask->setText(hex2(*op.mask));
        break;
    case OpType::Swap:
        select(swap_mode, op.a ? "fixed" : "random");
        if (op.a)
            swap_a->setText(hex4(*op.a));
        if (op.b)
            swap_b->setText(hex4(*op.b));
        select(swap_width, QString::number(op.width));
        break;
    case OpType::Copy:
        if (op.from)
        {
            select(copy_target, target_name(op.from->target));
            copy_first->setText(hex4(op.from->first));
            copy_last->setText(hex4(op.from->last));
        }
        copy_length->setValue(int(op.length));
        break;
    case OpType::Freeze:
        select(freeze_mode, op.value ? "value" : "hit");
        if (op.value)
            freeze_value->setText(hex2(*op.value));
        break;
    case OpType::Shuffle:
        shuffle_window->setValue(op.window);
        break;
    case OpType::Shift:
        shift_dx->setValue(op.dx);
        shift_dy->setValue(op.dy);
        shift_wrap->setChecked(op.wrap);
        shift_fill->setText(hex2(op.value.value_or(0)));
        break;
    case OpType::Flip:
        select(flip_axis, axis_name(op.axis));
        break;
    case OpType::Levels:
        select(levels_curve, curve_name(op.curve));
        levels_amount->setValue(op.amount);
        break;
    case OpType::Filter:
        select(filter_kernel, kernel_name(op.kernel));
        break;
    case OpType::Stamp:
        stamp_from->setText(hex4(op.source));
        break;
    case OpType::Fill:
        fill_value->setText(hex2(op.value.value_or(0)));
        break;
    }

    const Trigger &t = r.when;
    select(when_combo, trigger_name(t.type));
    every_frames->setValue(int(std::min<uint64_t>(t.every, INT_MAX)));
    chance->setValue(t.chance * 100.0);
    once_frame->setValue(int(std::min<uint64_t>(t.frame, INT_MAX)));
    hotkey->setValue(t.hotkey);
    window_enabled->setChecked(t.window.has_value());
    window_start->setValue(t.window ? int(std::min<uint64_t>(t.window->start, INT_MAX)) : 0);
    window_end->setValue(t.window ? int(std::min<uint64_t>(t.window->end, INT_MAX)) : 3600);

    select(role_combo, role_name(r.aim.role));
    {
        QSignalBlocker quiet(class_mode); // no suggestion while loading
        select(class_mode, !r.aim.classes.empty() ? "only" : !r.aim.except.empty() ? "except" : "all");
    }
    for (auto [c, check] : class_checks)
    {
        auto &list = !r.aim.classes.empty() ? r.aim.classes : r.aim.except;
        check->setChecked(std::find(list.begin(), list.end(), c) != list.end());
    }

    count_spin->setValue(int(std::min<uint32_t>(r.count, 1000000)));
    select(pick_combo, r.pick == Pick::Sticky ? "sticky" : "fresh");
    intensity_spin->setValue(r.intensity * 100.0);
    enabled_check->setChecked(r.enabled);
    avoid_check->setChecked(r.avoid_hot);

    // The region last: choosing it locks the range fields to it.
    if (!r.region.empty() && regions.count(r.region))
        select(region_combo, qs(r.region));
}

void RuleEditor::regionChosen()
{
    auto name = region_combo->currentData().toString().toStdString();
    auto it = regions.find(name);
    bool custom = it == regions.end();
    target_combo->setEnabled(custom);
    first_edit->setEnabled(custom);
    last_edit->setEnabled(custom);
    if (custom)
        return;
    select(target_combo, target_name(it->second.target));
    first_edit->setText(hex4(it->second.first));
    last_edit->setText(hex4(it->second.last));
}

RuleEditor::Built RuleEditor::build() const
{
    Built b;
    Rule &r = b.rule;
    QString &err = b.error;
    r = original; // salt and anything else the form doesn't show

    r.name = name_edit->text().trimmed().toStdString();
    if (r.name.empty())
        err = tr("name: give the rule a name");

    r.range.target = target_of(target_combo);
    r.region = region_combo->currentData().toString().toStdString();
    r.rect.reset();
    if (auto it = regions.find(r.region); it != regions.end())
        r.range = it->second;
    else if (shape_combo->currentData() == "rect")
    {
        auto first = number(first_edit, "from", 0, UINT32_MAX, err);
        set_rect(r, r.range.target, uint32_t(first.value_or(0)),
                 Rect{ uint32_t(rect_row->value()), uint32_t(rect_width->value()), uint32_t(rect_height->value()) });
    }
    else
    {
        r.region.clear();
        auto first = number(first_edit, "from", 0, UINT32_MAX, err);
        auto last = number(last_edit, "to", 0, UINT32_MAX, err);
        r.range.first = uint32_t(first.value_or(0));
        r.range.last = uint32_t(last.value_or(0));
    }

    Op op;
    op.type = parse_op(op_combo->currentData().toString().toStdString()).value_or(OpType::Random);
    switch (op.type)
    {
    case OpType::Random:
        op.min = random_min->value();
        op.max = random_max->value();
        break;
    case OpType::Add:
        op.min = add_min->value();
        op.max = add_max->value();
        op.clamp = add_clamp->isChecked();
        break;
    case OpType::Bitflip:
        op.bits = bitflip_bits->value();
        if (bitflip_mode->currentData() == "mask")
            if (auto m = number(bitflip_mask, "mask", 0, 255, err))
                op.mask = uint8_t(*m);
        break;
    case OpType::Swap:
        op.width = swap_width->currentData().toInt();
        if (swap_mode->currentData() == "fixed")
        {
            op.a = uint32_t(number(swap_a, "address A", 0, UINT32_MAX, err).value_or(0));
            op.b = uint32_t(number(swap_b, "address B", 0, UINT32_MAX, err).value_or(0));
        }
        break;
    case OpType::Copy:
    {
        AddressRange from{ target_of(copy_target), 0, 0 };
        from.first = uint32_t(number(copy_first, "copy from", 0, UINT32_MAX, err).value_or(0));
        from.last = uint32_t(number(copy_last, "copy to", 0, UINT32_MAX, err).value_or(0));
        op.from = from;
        op.length = uint32_t(copy_length->value());
        break;
    }
    case OpType::Freeze:
        if (freeze_mode->currentData() == "value")
            if (auto v = number(freeze_value, "value", 0, 255, err))
                op.value = uint8_t(*v);
        break;
    case OpType::Shuffle:
        op.window = shuffle_window->value();
        break;
    case OpType::Shift:
        op.dx = shift_dx->value();
        op.dy = shift_dy->value();
        op.wrap = shift_wrap->isChecked();
        if (!op.wrap)
            if (auto v = number(shift_fill, "fill", 0, 255, err))
                op.value = uint8_t(*v);
        break;
    case OpType::Flip:
        op.axis = parse_axis(flip_axis->currentData().toString().toStdString()).value_or(Axis::Horizontal);
        break;
    case OpType::Levels:
        op.curve = parse_curve(levels_curve->currentData().toString().toStdString()).value_or(Curve::Invert);
        op.amount = op.curve == Curve::Invert ? 0.0 : levels_amount->value();
        break;
    case OpType::Filter:
        op.kernel = parse_kernel(filter_kernel->currentData().toString().toStdString()).value_or(Kernel::Blur);
        break;
    case OpType::Stamp:
        op.source = uint32_t(number(stamp_from, "copy from", 0, UINT32_MAX, err).value_or(0));
        break;
    case OpType::Fill:
        if (auto v = number(fill_value, "value", 0, 255, err))
            op.value = uint8_t(*v);
        break;
    }
    r.op = op;

    Trigger t;
    t.type = parse_trigger(when_combo->currentData().toString().toStdString()).value_or(TriggerType::EveryFrames);
    switch (t.type)
    {
    case TriggerType::EveryFrames: t.every = uint32_t(every_frames->value()); break;
    case TriggerType::ChancePerFrame: t.chance = chance->value() / 100.0; break;
    case TriggerType::OnceAtFrame: t.frame = uint64_t(once_frame->value()); break;
    case TriggerType::Hotkey: t.hotkey = hotkey->value(); break;
    case TriggerType::AtLoad: break;
    }
    if (window_enabled->isChecked() && t.type != TriggerType::AtLoad)
        t.window = FrameWindow{ uint64_t(window_start->value()), uint64_t(window_end->value()) };
    r.when = t;

    r.count = uint32_t(count_spin->value());
    r.pick = pick_combo->currentData() == "sticky" ? Pick::Sticky : Pick::Fresh;
    r.intensity = intensity_spin->value() / 100.0;
    r.enabled = enabled_check->isChecked();
    r.avoid_hot = avoid_check->isChecked();

    // The aim, for ROM rules only; code ops always aim at opcodes.
    r.aim = {};
    if (r.range.target == Target::ROM)
    {
        if (!is_code_op(op.type))
            r.aim.role = parse_role(role_combo->currentData().toString().toStdString()).value_or(Role::Any);
        std::vector<OpClass> ticked;
        for (auto [c, check] : class_checks)
            if (check->isChecked())
                ticked.push_back(c);
        if (class_mode->currentData() == "only")
        {
            r.aim.classes = ticked;
            if (ticked.empty() && err.isEmpty())
                err = tr("instructions: tick at least one, or choose Any instruction");
        }
        else if (class_mode->currentData() == "except")
            r.aim.except = ticked;
    }

    if (err.isEmpty())
        if (auto problem = validate_rule(r, sizes))
            err = qs(*problem);
    return b;
}

void RuleEditor::fitToContents()
{
    // As tall as the sections need, up to most of the screen; the rest scrolls.
    auto want = sections->sizeHint();
    int room = screen() ? screen()->availableGeometry().height() * 3 / 4 : 800;
    scroll->setMinimumWidth(want.width() + scroll->verticalScrollBar()->sizeHint().width());
    scroll->setMinimumHeight(std::min(want.height(), room));
    adjustSize();
}

Rule RuleEditor::rule() const
{
    return build().rule;
}

QString RuleEditor::error() const
{
    return build().error;
}

void RuleEditor::refresh()
{
    op_pages->setCurrentIndex(op_combo->currentIndex());
    when_pages->setCurrentIndex(when_combo->currentIndex());
    bitflip_bits->setEnabled(bitflip_mode->currentData() == "bits");
    bitflip_mask->setEnabled(bitflip_mode->currentData() == "mask");
    swap_a->setEnabled(swap_mode->currentData() == "fixed");
    swap_b->setEnabled(swap_mode->currentData() == "fixed");
    freeze_value->setEnabled(freeze_mode->currentData() == "value");
    shift_fill->setEnabled(!shift_wrap->isChecked());
    levels_amount->setEnabled(levels_curve->currentData() != QString(curve_name(Curve::Invert)));
    bool custom_range = region_combo->currentData().toString().isEmpty();
    bool rect = shape_combo->currentData() == "rect";
    shape_combo->setEnabled(custom_range);
    rect_fields->setEnabled(custom_range && rect);
    last_edit->setEnabled(custom_range && !rect);
    auto area_op = is_area_op(parse_op(op_combo->currentData().toString().toStdString()).value_or(OpType::Random));
    count_spin->setEnabled(!area_op); // area ops act on the whole area
    pick_combo->setEnabled(!area_op);
    bool rom = target_of(target_combo) == Target::ROM;
    bool code_op = is_code_op(parse_op(op_combo->currentData().toString().toStdString()).value_or(OpType::Random));
    aim_box->setEnabled(rom && !area_op);
    role_combo->setEnabled(!code_op);
    // Sections that don't apply are hidden, and the pages take only the room they need;
    // the dialog fits itself again when that changes.
    bool show_aim = rom && !area_op, show_rect = custom_range && rect;
    bool layout_changed = aim_box->isVisibleTo(this) != show_aim || no_aim_hint->isVisibleTo(this) == rom ||
                          shown_page != op_pages->currentIndex() || shown_when_page != when_pages->currentIndex() ||
                          rect_fields->isVisibleTo(this) != show_rect;
    aim_box->setVisible(show_aim);
    no_aim_hint->setVisible(!rom);
    where_form->setRowVisible(rect_fields, show_rect);
    for (auto *pages : { op_pages, when_pages })
        for (int i = 0; i < pages->count(); i++)
            pages->widget(i)->setSizePolicy(QSizePolicy::Preferred, i == pages->currentIndex() ? QSizePolicy::Preferred
                                                                                                : QSizePolicy::Ignored);
    shown_page = op_pages->currentIndex();
    shown_when_page = when_pages->currentIndex();
    if (layout_changed)
        QTimer::singleShot(0, this, [this] { fitToContents(); });
    for (auto [c, check] : class_checks)
        check->setEnabled(class_mode->currentData() != "all");
    bool at_load = when_combo->currentData() == QString(trigger_name(TriggerType::AtLoad));
    window_enabled->setEnabled(!at_load); // the run's start isn't a frame
    window_start->setEnabled(window_enabled->isChecked());
    window_end->setEnabled(window_enabled->isChecked());

    auto b = build();
    if (custom_range && rect)
    {
        QSignalBlocker quiet(last_edit);
        last_edit->setText(hex4(b.rule.range.last)); // the rectangle's bottom-right byte
    }
    summary_label->setText(qs(describe_rule(b.rule)));
    error_label->setText(b.error);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(b.error.isEmpty());
}
