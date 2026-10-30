#include "RuleEditor.hpp"
#include "corrupt/rule_text.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <climits>

using namespace corrupt;

namespace
{

constexpr OpType kOps[] = { OpType::Random, OpType::Add, OpType::Bitflip, OpType::Swap,
                            OpType::Copy, OpType::Freeze, OpType::Shuffle };
constexpr TriggerType kTriggers[] = { TriggerType::EveryFrames, TriggerType::ChancePerFrame,
                                      TriggerType::OnceAtFrame, TriggerType::Hotkey };

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
    auto layout = new QVBoxLayout(this);

    auto top = new QFormLayout;
    name_edit = line(this, "name");
    top->addRow(tr("Name"), name_edit);
    layout->addLayout(top);

    // Where
    auto where_box = new QGroupBox(tr("Where"), this);
    auto where = new QFormLayout(where_box);
    target_combo = combo(where_box, "target");
    fill_targets(target_combo);
    where->addRow(tr("Memory"), target_combo);
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
    where->addRow(tr("From"), range_row);
    layout->addWidget(where_box);

    // What
    auto what_box = new QGroupBox(tr("What"), this);
    auto what = new QVBoxLayout(what_box);
    op_combo = combo(what_box, "op");
    for (auto t : kOps)
        op_combo->addItem(op_label(t), QString(op_name(t)));
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
    f->addRow(tr("Lowest"), random_min);
    f->addRow(tr("Highest"), random_max);

    auto add_page = page(f);
    add_min = spin(add_page, "add_min", -255, 255);
    add_max = spin(add_page, "add_max", -255, 255);
    add_clamp = new QCheckBox(tr("Clamp at 0 and 255 instead of wrapping"), add_page);
    add_clamp->setObjectName("add_clamp");
    f->addRow(tr("Smallest change"), add_min);
    f->addRow(tr("Largest change"), add_max);
    f->addRow(add_clamp);

    auto bitflip_page = page(f);
    bitflip_mode = combo(bitflip_page, "bitflip_mode");
    bitflip_mode->addItem(tr("Random bits"), QString("bits"));
    bitflip_mode->addItem(tr("XOR with a mask"), QString("mask"));
    bitflip_bits = spin(bitflip_page, "bitflip_bits", 1, 8);
    bitflip_mask = line(bitflip_page, "bitflip_mask", "0x81");
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
    f->addRow(tr("Copy from"), copy_target);
    f->addRow(tr("Source range"), copy_row);
    f->addRow(tr("Bytes per hit"), copy_length);

    auto freeze_page = page(f);
    freeze_mode = combo(freeze_page, "freeze_mode");
    freeze_mode->addItem(tr("Hold whatever it becomes when hit"), QString("hit"));
    freeze_mode->addItem(tr("Hold a value"), QString("value"));
    freeze_value = line(freeze_page, "freeze_value", "0x00");
    f->addRow(tr("Mode"), freeze_mode);
    f->addRow(tr("Value"), freeze_value);

    auto shuffle_page = page(f);
    shuffle_window = spin(shuffle_page, "shuffle_window", 2, 256);
    f->addRow(tr("Window (bytes)"), shuffle_window);
    layout->addWidget(what_box);

    // When
    auto when_box = new QGroupBox(tr("When"), this);
    auto when = new QVBoxLayout(when_box);
    when_combo = combo(when_box, "when");
    for (auto t : kTriggers)
        when_combo->addItem(trigger_label(t), QString(trigger_name(t)));
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
    when_page(tr("Hotkey"), hotkey);

    auto window_row = new QHBoxLayout;
    window_enabled = new QCheckBox(tr("Only between frames"), when_box);
    window_enabled->setObjectName("window_enabled");
    window_start = spin(when_box, "window_start", 0, INT_MAX);
    window_end = spin(when_box, "window_end", 0, INT_MAX);
    window_row->addWidget(window_enabled);
    window_row->addWidget(window_start, 1);
    window_row->addWidget(new QLabel(tr("and"), when_box));
    window_row->addWidget(window_end, 1);
    when->addLayout(window_row);
    auto frames_hint = new QLabel(tr("Frames count from turning the recipe on; 60 is about a second."), when_box);
    frames_hint->setWordWrap(true);
    frames_hint->setEnabled(false);
    when->addWidget(frames_hint);
    layout->addWidget(when_box);

    // How much
    auto how_box = new QGroupBox(tr("How Much"), this);
    auto how = new QFormLayout(how_box);
    count_spin = spin(how_box, "count", 1, 1000000);
    how->addRow(tr("Addresses per firing"), count_spin);
    pick_combo = combo(how_box, "pick");
    pick_combo->addItem(tr("New addresses every time"), QString("fresh"));
    pick_combo->addItem(tr("Pick once, keep hitting them"), QString("sticky"));
    how->addRow(tr("Pick"), pick_combo);
    intensity_spin = new CompactSpinBox(how_box);
    intensity_spin->setObjectName("intensity");
    intensity_spin->setRange(0.0, 1.0);
    intensity_spin->setDecimals(4);
    intensity_spin->setSingleStep(0.05);
    how->addRow(tr("Intensity"), intensity_spin);
    enabled_check = new QCheckBox(tr("Enabled"), how_box);
    enabled_check->setObjectName("enabled");
    how->addRow(enabled_check);
    layout->addWidget(how_box);

    summary_label = new QLabel(this);
    summary_label->setObjectName("summary");
    summary_label->setWordWrap(true);
    layout->addWidget(summary_label);
    error_label = new QLabel(this);
    error_label->setObjectName("error");
    error_label->setWordWrap(true);
    error_label->setStyleSheet("color: #d33;");
    layout->addWidget(error_label);

    buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName("buttons");
    layout->addWidget(buttons);
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

    count_spin->setValue(int(std::min<uint32_t>(r.count, 1000000)));
    select(pick_combo, r.pick == Pick::Sticky ? "sticky" : "fresh");
    intensity_spin->setValue(r.intensity);
    enabled_check->setChecked(r.enabled);

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
    r = original; // salt, avoid_hot and anything else the form doesn't show

    r.name = name_edit->text().trimmed().toStdString();
    if (r.name.empty())
        err = tr("name: give the rule a name");

    r.range.target = target_of(target_combo);
    r.region = region_combo->currentData().toString().toStdString();
    if (auto it = regions.find(r.region); it != regions.end())
        r.range = it->second;
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
    }
    if (window_enabled->isChecked())
        t.window = FrameWindow{ uint64_t(window_start->value()), uint64_t(window_end->value()) };
    r.when = t;

    r.count = uint32_t(count_spin->value());
    r.pick = pick_combo->currentData() == "sticky" ? Pick::Sticky : Pick::Fresh;
    r.intensity = intensity_spin->value();
    r.enabled = enabled_check->isChecked();

    if (err.isEmpty())
        if (auto problem = validate_rule(r, sizes))
            err = qs(*problem);
    return b;
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
    window_start->setEnabled(window_enabled->isChecked());
    window_end->setEnabled(window_enabled->isChecked());

    auto b = build();
    summary_label->setText(qs(describe_rule(b.rule)));
    error_label->setText(b.error);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(b.error.isEmpty());
}
