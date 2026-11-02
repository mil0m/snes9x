#include "FunctionEditor.hpp"
#include "UiKit.hpp"

#include "corrupt/code_aim.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

using namespace corrupt;

namespace
{

enum Col
{
    Address,
    InstructionCol,
    Becomes,
    Now,
    Cols
};

enum Page
{
    NumberPage,
    BranchPage,
    MathPage,
    CallPage,
};

QString cpu(std::optional<uint32_t> address, uint32_t offset)
{
    if (!address)
        return QString::fromStdString(hex_address(offset, Target::ROM));
    return QString("$%1:%2").arg(*address >> 16, 2, 16, QChar('0')).arg(*address & 0xFFFF, 4, 16, QChar('0')).toUpper();
}

QString mnemonic(uint8_t op)
{
    return QString::fromLatin1(op_info(op).mnemonic);
}

QString becomes(const ChangeOption &o)
{
    switch (o.kind)
    {
    case ChangeKind::Number:
        return o.other ? QObject::tr("another number, or %1").arg(mnemonic(o.other)) : QObject::tr("another number");
    case ChangeKind::Branch:
        return QObject::tr("the opposite (%1), always or never").arg(mnemonic(flipped_branch(o.opcode)));
    case ChangeKind::Math:
        return mnemonic(o.other);
    case ChangeKind::Skip:
    case ChangeKind::Redirect:
        return QObject::tr("skipped, or a call to another function");
    }
    return {};
}

// The instruction as the given bytes make it.
QString text_of(const ChangeOption &o, const std::vector<uint8_t> &bytes)
{
    bool narrow = o.kind != ChangeKind::Number || o.width == 1;
    auto d = disassemble(bytes, narrow, narrow, o.address);
    if (bytes.size() >= 2 && bytes[0] == 0xEA && bytes[1] == 0xEA)
        return QObject::tr("NOPs");
    return QString::fromStdString(d.text);
}

QString function_label(const FunctionRow &r)
{
    QString where = cpu(r.address, r.f.entry);
    return r.label.empty() ? where : where + " · " + QString::fromStdString(r.label);
}

} // namespace

FunctionEditor::FunctionEditor(FunctionSource &source, const FunctionRow &row, QWidget *parent)
    : QDialog(parent), source(source), function(row)
{
    setObjectName("function_editor");
    setWindowTitle(tr("Change %1").arg(cpu(row.address, row.f.entry)));
    resize(680, 560);
    auto layout = new QVBoxLayout(this);
    layout->addWidget(ui::heading(tr("Change %1").arg(function_label(row)), this));
    auto intro = new QLabel(tr("Its code, line by line, and what each line can become without breaking the shape of "
                               "the code: another number, the opposite decision, the other half of a sum, or a call "
                               "skipped or sent to another function. <b>Apply</b> changes it at the next frame; "
                               "<b>Put Back</b> undoes it. If a change breaks the game, the crash guard puts it "
                               "back. Nothing else corrupts this function while you change it."),
                            this);
    intro->setWordWrap(true);
    ui::setTone(intro, ui::Tone::Secondary);
    layout->addWidget(intro);

    not_running = new ui::StatusLine(this);
    not_running->setObjectName("not_running");
    not_running->setMessage(tr("Changes work while corruption is on: press Corrupt (F7) in the panel first."),
                            ui::Tone::Warning);
    layout->addWidget(not_running);

    empty_label = new QLabel(tr("Nothing in its code can change safely yet: no numbers, decisions, sums or calls "
                                "the code map has seen run. Play on and look again."),
                             this);
    empty_label->setObjectName("empty");
    empty_label->setWordWrap(true);
    layout->addWidget(empty_label);

    table = new QTableWidget(0, Cols, this);
    table->setObjectName("lines");
    table->setAccessibleName(tr("Lines of its code"));
    table->setHorizontalHeaderLabels({ tr("Address"), tr("Instruction"), tr("Can Become"), tr("Now") });
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(Now, QHeaderView::Stretch);
    layout->addWidget(table, 1);

    // What the selected line becomes.
    editor = new QWidget(this);
    auto editor_layout = new QVBoxLayout(editor);
    editor_layout->setContentsMargins(0, 0, 0, 0);
    pages = new QStackedWidget(editor);

    auto number_page = new QWidget(pages);
    auto number_form = ui::form(number_page);
    number_form->setContentsMargins(0, 0, 0, 0);
    number = new QSpinBox(number_page);
    number->setObjectName("number");
    number->setDisplayIntegerBase(16);
    number->setPrefix("$");
    number->setToolTip(tr("The value the instruction uses instead (hexadecimal)"));
    number->setAccessibleName(tr("New value"));
    swap_math = new QCheckBox(number_page);
    swap_math->setObjectName("swap_math");
    auto number_row = new QHBoxLayout;
    number_row->addWidget(number);
    number_row->addWidget(swap_math);
    number_row->addStretch(1);
    auto value_label = new QLabel(tr("&Value"), number_page);
    value_label->setBuddy(number);
    number_form->addRow(value_label, number_row);
    pages->addWidget(number_page);

    auto branch_page = new QWidget(pages);
    auto branch_form = ui::form(branch_page);
    branch_form->setContentsMargins(0, 0, 0, 0);
    branch = new QComboBox(branch_page);
    branch->setObjectName("branch");
    branch->setAccessibleName(tr("What the decision becomes"));
    auto branch_label = new QLabel(tr("&Becomes"), branch_page);
    branch_label->setBuddy(branch);
    branch_form->addRow(branch_label, ui::row(branch));
    pages->addWidget(branch_page);

    auto math_page = new QWidget(pages);
    auto math_layout = new QVBoxLayout(math_page);
    math_layout->setContentsMargins(0, 0, 0, 0);
    math_label = new QLabel(math_page);
    math_label->setObjectName("math");
    math_layout->addWidget(math_label);
    pages->addWidget(math_page);

    auto call_page = new QWidget(pages);
    auto call_form = ui::form(call_page);
    call_form->setContentsMargins(0, 0, 0, 0);
    call = new QComboBox(call_page);
    call->setObjectName("call");
    call->setAccessibleName(tr("What the call does instead"));
    call->setToolTip(tr("Skip it, or call another function the game has: a near call can only go to near "
                        "functions in its own bank, a long call to any long function"));
    auto call_label = new QLabel(tr("&Instead"), call_page);
    call_label->setBuddy(call);
    call_form->addRow(call_label, ui::row(call));
    pages->addWidget(call_page);
    editor_layout->addWidget(pages);

    // A timer (#61): on for a while, again and again.
    auto timer_row = new QHBoxLayout;
    timer_check = new QCheckBox(tr("Kept with a timer: on for"), editor);
    timer_check->setObjectName("timer");
    timer_check->setToolTip(tr("When kept in the recipe, it turns on and off by itself, counted from the start of the "
                               "run. Short times make it flicker."));
    auto seconds = [&](const char *name, double value, const QString &what) {
        auto s = new QDoubleSpinBox(editor);
        s->setObjectName(name);
        s->setRange(0.1, 600);
        s->setDecimals(1);
        s->setSingleStep(0.5);
        s->setSuffix(tr(" s"));
        s->setValue(value);
        s->setAccessibleName(what);
        return s;
    };
    timer_for = seconds("timer_for", 2, tr("On for how long"));
    timer_every = seconds("timer_every", 10, tr("Every how long"));
    timer_row->addWidget(timer_check);
    timer_row->addWidget(timer_for);
    timer_row->addWidget(new QLabel(tr("every"), editor));
    timer_row->addWidget(timer_every);
    timer_row->addStretch(1);
    editor_layout->addLayout(timer_row);

    auto apply_row = new QHBoxLayout;
    apply_button = new QPushButton(ui::icon("bolt"), tr("Apply"), editor);
    apply_button->setObjectName("apply");
    apply_button->setAutoDefault(false);
    apply_button->setToolTip(tr("Change it at the next frame"));
    put_back_button = new QPushButton(ui::icon("arrow-back-up"), tr("Put Back"), editor);
    put_back_button->setObjectName("put_back");
    put_back_button->setAutoDefault(false);
    put_back_button->setToolTip(tr("The game's own instruction again, at the next frame"));
    hotkey = new QComboBox(editor);
    hotkey->setObjectName("hotkey");
    hotkey->setAccessibleName(tr("Button for the change"));
    hotkey->addItem(tr("No button"));
    for (int k = 1; k <= 9; k++)
        hotkey->addItem(tr("Button %1").arg(k));
    hotkey->setToolTip(tr("A button in the corruption panel (and on the remote page) that turns the change on, and off again"));
    start_on = new QCheckBox(tr("Starts on"), editor);
    start_on->setObjectName("start_on");
    start_on->setChecked(true);
    start_on->setToolTip(tr("On from the start of every run"));
    keep_button = new QPushButton(ui::icon("pin"), tr("Keep in Recipe"), editor);
    keep_button->setObjectName("keep");
    keep_button->setAutoDefault(false);
    forget_button = new QPushButton(ui::icon("pinned-off"), tr("Remove"), editor);
    forget_button->setObjectName("forget");
    forget_button->setAutoDefault(false);
    forget_button->setToolTip(tr("Take the change out of the recipe. The code stays as it is now."));
    apply_row->addWidget(apply_button);
    apply_row->addWidget(put_back_button);
    apply_row->addStretch(1);
    apply_row->addWidget(hotkey);
    apply_row->addWidget(start_on);
    apply_row->addWidget(keep_button);
    apply_row->addWidget(forget_button);
    editor_layout->addLayout(apply_row);
    layout->addWidget(editor);

    message = new ui::StatusLine(this);
    message->setObjectName("message");
    layout->addWidget(message);
    layout->addStretch(0);

    auto bottom = new QHBoxLayout;
    auto close_button = new QPushButton(tr("Close"), this);
    close_button->setAutoDefault(false);
    bottom->addStretch(1);
    bottom->addWidget(close_button);
    layout->addLayout(bottom);

    timer = new QTimer(this);
    timer->setInterval(500);
    connect(timer, &QTimer::timeout, this, &FunctionEditor::update);
    connect(close_button, &QPushButton::clicked, this, &QDialog::close);
    connect(table, &QTableWidget::itemSelectionChanged, this, &FunctionEditor::selectionChanged);
    connect(apply_button, &QPushButton::clicked, this, &FunctionEditor::apply);
    connect(put_back_button, &QPushButton::clicked, this, &FunctionEditor::putBack);
    connect(keep_button, &QPushButton::clicked, this, &FunctionEditor::keep);
    connect(forget_button, &QPushButton::clicked, this, &FunctionEditor::forget);
    message->hide();
    refresh();
}

void FunctionEditor::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    timer->start();
}

void FunctionEditor::hideEvent(QHideEvent *event)
{
    timer->stop();
    QDialog::hideEvent(event);
}

void FunctionEditor::say(const QString &text, int tone)
{
    message->setMessage(text, ui::Tone(tone));
    message->show();
}

const Change *FunctionEditor::kept(uint32_t at) const
{
    for (auto &c : function.changes)
        if (c.at == at)
            return &c;
    return nullptr;
}

void FunctionEditor::refresh()
{
    options = source.changeOptions(function.f.entry);
    for (auto &r : source.functions())
        if (r.f.entry == function.f.entry)
            function = r;
    table->setRowCount(int(options.size()));
    QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for (size_t i = 0; i < options.size(); i++)
    {
        auto &o = options[i];
        auto set = [&](int col, const QString &text, bool mono) {
            auto *it = new QTableWidgetItem(text);
            if (mono)
                it->setFont(fixed);
            table->setItem(int(i), col, it);
        };
        set(Address, cpu(o.address, o.at), true);
        set(InstructionCol, QString::fromStdString(o.text), true);
        set(Becomes, becomes(o), false);
        set(Now, QString(), false);
    }
    bool any = !options.empty();
    table->setVisible(any);
    empty_label->setVisible(!any);
    update();
    selectionChanged();
}

void FunctionEditor::update()
{
    for (auto &r : source.functions())
        if (r.f.entry == function.f.entry)
            function.changes = r.changes;
    for (size_t i = 0; i < options.size() && int(i) < table->rowCount(); i++)
    {
        auto &o = options[i];
        auto now = source.romNow(o.at, o.length);
        auto clean = source.romClean(o.at, o.length);
        QStringList parts;
        if (now != clean)
            parts << tr("now %1").arg(text_of(o, now));
        if (auto *k = kept(o.at))
            parts << (k->hotkey ? tr("kept, button %1").arg(k->hotkey) : tr("kept")) +
                         (k->start_on ? tr(", starts on") : QString()) +
                         (k->every ? tr(", on %1 s every %2 s").arg(k->for_frames / 60.0).arg(k->every / 60.0) : QString());
        if (auto *it = table->item(int(i), Now); it && it->text() != parts.join(" · "))
            it->setText(parts.join(" · "));
    }
    not_running->setVisible(!source.running());
}

int FunctionEditor::selectedRow() const
{
    auto rows = table->selectionModel() ? table->selectionModel()->selectedRows() : QModelIndexList{};
    if (rows.isEmpty() || size_t(rows.front().row()) >= options.size())
        return -1;
    return rows.front().row();
}

void FunctionEditor::selectionChanged()
{
    int row = selectedRow();
    editor->setVisible(row >= 0);
    if (row < 0)
        return;
    auto &o = options[size_t(row)];
    auto *k = kept(o.at);
    switch (o.kind)
    {
    case ChangeKind::Number: {
        pages->setCurrentIndex(NumberPage);
        number->setRange(0, o.width == 2 ? 0xFFFF : 0xFF);
        uint32_t value = o.value;
        if (k && k->value.size() >= size_t(1 + o.width))
            value = k->value[1] | (o.width == 2 ? k->value[2] << 8 : 0);
        number->setValue(int(value));
        swap_math->setVisible(o.other != 0);
        swap_math->setText(tr("and make it %1").arg(mnemonic(o.other)));
        swap_math->setChecked(k && !k->value.empty() && k->value[0] == o.other);
        break;
    }
    case ChangeKind::Branch:
        pages->setCurrentIndex(BranchPage);
        branch->clear();
        branch->addItem(tr("The opposite: %1").arg(mnemonic(flipped_branch(o.opcode))), int(BranchTo::Opposite));
        branch->addItem(tr("Always (BRA)"), int(BranchTo::Always));
        branch->addItem(tr("Never (two NOPs)"), int(BranchTo::Never));
        if (k && !k->value.empty())
            branch->setCurrentIndex(k->value[0] == 0x80 ? 1 : k->value[0] == 0xEA ? 2 : 0);
        break;
    case ChangeKind::Math:
        pages->setCurrentIndex(MathPage);
        math_label->setText(tr("%1 becomes %2: the sum goes the other way.")
                                .arg(QString::fromStdString(o.text), mnemonic(o.other)));
        break;
    case ChangeKind::Skip:
    case ChangeKind::Redirect: {
        pages->setCurrentIndex(CallPage);
        call->clear();
        callable.clear();
        call->addItem(tr("Skip the call"));
        for (auto &r : source.functions())
        {
            if (r.f.entry == function.f.entry || (o.target && r.f.entry == *o.target) || r.f.type != o.call)
                continue;
            if (o.call == CallType::Near && (!r.address || !o.address || (*r.address >> 16) != (*o.address >> 16)))
                continue; // a JSR stays in its bank
            callable.push_back(r);
            call->addItem(tr("Call %1 instead").arg(function_label(r)));
            if (callable.size() >= 200)
                break;
        }
        if (k && k->kind == ChangeKind::Redirect && k->value.size() >= 3)
            for (size_t i = 0; i < callable.size(); i++)
                if (auto bytes = source.redirectBytes(o, callable[i].f.entry); bytes && *bytes == k->value)
                    call->setCurrentIndex(int(i) + 1);
        break;
    }
    }
    hotkey->setCurrentIndex(k ? k->hotkey : 0);
    start_on->setChecked(!k || k->start_on);
    timer_check->setChecked(k && k->every);
    if (k && k->every)
    {
        timer_for->setValue(k->for_frames / 60.0);
        timer_every->setValue(k->every / 60.0);
    }
    forget_button->setEnabled(k != nullptr);
}

std::optional<Change> FunctionEditor::wanted(QString &why) const
{
    int row = selectedRow();
    if (row < 0)
        return std::nullopt;
    auto &o = options[size_t(row)];
    ChangeKind kind = o.kind;
    std::vector<uint8_t> value;
    switch (o.kind)
    {
    case ChangeKind::Number:
        value = number_bytes(o, uint32_t(number->value()));
        if (swap_math->isVisible() && swap_math->isChecked())
            value[0] = o.other;
        break;
    case ChangeKind::Branch:
        value = branch_bytes(o, BranchTo(branch->currentData().toInt()));
        break;
    case ChangeKind::Math:
        value = math_bytes(o);
        break;
    case ChangeKind::Skip:
    case ChangeKind::Redirect:
        if (call->currentIndex() <= 0)
        {
            kind = ChangeKind::Skip;
            value = skip_bytes(o);
        }
        else
        {
            auto bytes = source.redirectBytes(o, callable[size_t(call->currentIndex() - 1)].f.entry);
            if (!bytes)
            {
                why = tr("a call can't go there");
                return std::nullopt;
            }
            kind = ChangeKind::Redirect;
            value = *bytes;
        }
        break;
    }
    Change c;
    c.function = function.f.entry;
    c.at = o.at;
    c.kind = kind;
    c.was = source.romClean(o.at, value.size());
    c.value = value;
    if (c.was.size() != c.value.size())
    {
        why = tr("it runs past the end of the ROM");
        return std::nullopt;
    }
    if (c.value == c.was)
    {
        why = tr("that's what the game has there already");
        return std::nullopt;
    }
    c.hotkey = hotkey->currentIndex();
    c.start_on = start_on->isChecked();
    if (timer_check->isChecked())
    {
        c.every = uint32_t(std::lround(timer_every->value() * 60));
        c.for_frames = uint32_t(std::lround(timer_for->value() * 60));
        if (c.for_frames >= c.every)
        {
            why = tr("the timer's on time has to be shorter than its cycle");
            return std::nullopt;
        }
    }
    c.note = function.label;
    return c;
}

void FunctionEditor::apply()
{
    QString why;
    auto c = wanted(why);
    if (!c)
        return say(tr("Nothing to apply: %1.").arg(why), int(ui::Tone::Warning));
    auto err = source.applyChange(*c, true);
    if (!err.empty())
        return say(tr("Couldn't change it: %1").arg(QString::fromStdString(err)), int(ui::Tone::Error));
    say(tr("Changed at the next frame. Watch the game; Put Back undoes it, Keep in Recipe keeps it."), int(ui::Tone::Ok));
    update();
}

void FunctionEditor::putBack()
{
    int row = selectedRow();
    if (row < 0)
        return;
    auto &o = options[size_t(row)];
    Change c;
    c.function = function.f.entry;
    c.at = o.at;
    c.was = source.romClean(o.at, o.length);
    c.value = source.romNow(o.at, o.length);
    if (c.value == c.was)
        return say(tr("It's the game's own instruction already."));
    auto err = source.applyChange(c, false);
    if (!err.empty())
        return say(tr("Couldn't put it back: %1").arg(QString::fromStdString(err)), int(ui::Tone::Error));
    say(tr("Put back at the next frame."));
    update();
}

void FunctionEditor::keep()
{
    QString why;
    auto c = wanted(why);
    if (!c)
        return say(tr("Nothing to keep: %1.").arg(why), int(ui::Tone::Warning));
    auto err = source.keepChange(*c);
    if (!err.empty())
        return say(tr("Couldn't keep it: %1").arg(QString::fromStdString(err)), int(ui::Tone::Error));
    say(c->hotkey ? tr("Kept in the recipe on button %1. Save the recipe to keep it.").arg(c->hotkey)
                  : tr("Kept in the recipe. Save the recipe to keep it."),
        int(ui::Tone::Ok));
    update();
    selectionChanged();
}

void FunctionEditor::forget()
{
    int row = selectedRow();
    if (row < 0)
        return;
    source.forgetChange(options[size_t(row)].at);
    say(tr("Removed from the recipe. The code stays as it is now."));
    update();
    selectionChanged();
}
