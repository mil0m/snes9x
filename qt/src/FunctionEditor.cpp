#include "FunctionEditor.hpp"
#include "UiKit.hpp"
#include "corrupt/instruction_help.h"

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
    RiskCol,
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

// Sorts by a key of its own (the risk column: low first, then by place in the code).
class KeyedItem : public QTableWidgetItem
{
  public:
    KeyedItem(const QString &text, qint64 key) : QTableWidgetItem(text), key(key) {}
    bool operator<(const QTableWidgetItem &other) const override
    {
        auto *o = dynamic_cast<const KeyedItem *>(&other);
        return o ? key < o->key : QTableWidgetItem::operator<(other);
    }

  private:
    qint64 key;
};

QString risk_word(Risk r)
{
    switch (r)
    {
    case Risk::Low: return QObject::tr("Low");
    case Risk::Medium: return QObject::tr("Medium");
    case Risk::High: return QObject::tr("High");
    case Risk::Unclear: return QObject::tr("Unclear");
    }
    return {};
}

ui::Tone risk_tone(Risk r)
{
    return r == Risk::High ? ui::Tone::Error : r == Risk::Medium ? ui::Tone::Warning : ui::Tone::Normal;
}

// What the value feeds into, in a few words, for the risk column.
QString feed_words(Feed f)
{
    switch (f)
    {
    case Feed::Variable: return QObject::tr("a value in memory");
    case Feed::Scratch: return QObject::tr("a temporary byte");
    case Feed::Hardware: return QObject::tr("a hardware register");
    case Feed::Index: return QObject::tr("an index");
    case Feed::LoopCount: return QObject::tr("a loop's count");
    case Feed::LoopBound: return QObject::tr("a loop's end");
    case Feed::Condition: return QObject::tr("a decision");
    case Feed::Pointer: return QObject::tr("a pointer");
    case Feed::Argument: return QObject::tr("a call's input");
    case Feed::Overwritten: return QObject::tr("nothing: replaced at once");
    case Feed::LoopBranch: return QObject::tr("a loop");
    case Feed::ForwardBranch: return QObject::tr("a block it skips");
    case Feed::ResultUsed: return QObject::tr("its result is used");
    case Feed::ResultUnused: return QObject::tr("its result isn't used");
    case Feed::UnseenPath: return QObject::tr("a way that never ran");
    case Feed::WaitLoop: return QObject::tr("a wait");
    case Feed::WidthPath: return QObject::tr("the register width");
    case Feed::StackPath: return QObject::tr("the stack");
    case Feed::Unknown: return QObject::tr("can't tell");
    }
    return {};
}

// The whole story for the selected line: the risk, why, and what was measured.
QString risk_story(const FeedReading &r)
{
    auto risk = risk_of(r.feed);
    QString why = QString::fromStdString(r.why);
    if (!why.isEmpty())
        why[0] = why[0].toUpper();
    QString level = risk == Risk::Unclear ? QObject::tr("<b>Risk unclear.</b>") : QObject::tr("<b>%1 risk.</b>").arg(risk_word(risk));
    int failed = measured_failures(r.feed);
    QString measured = failed < 0 ? QObject::tr("Too few changes like this were measured to say how often they stop the game.")
                                  : QObject::tr("In tests on three games, about %1 in 100 changes like this stopped the "
                                                "game within 30 seconds.")
                                        .arg(failed);
    if (r.feed == Feed::UnseenPath)
        measured += QObject::tr(" Several such changes together add up, though: each sends the game down code it "
                                "has never run, so it counts as medium.");
    return level + " " + why.toHtmlEscaped() + ". " + measured;
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
    resize(680, 640);
    auto layout = new QVBoxLayout(this);
    layout->addWidget(ui::heading(tr("Change %1").arg(function_label(row)), this));
    auto intro = new QLabel(tr("Its code, line by line, and what each line can become without breaking the shape of "
                               "the code: another number, the opposite decision, the other half of a sum, or a call "
                               "skipped or sent to another function. <b>Apply</b> changes it at the next frame; "
                               "<b>Put Back</b> undoes it. If a change breaks the game, the crash guard goes back to "
                               "before the game first ran it and puts it back. Nothing else corrupts this function while you change it. <b>Risk</b> says how often a change "
                               "like it stopped the game in tests, from where the changed value goes next; <b>Try 5 s "
                               "Ahead</b> plays this change ahead, unseen, and says what it did here and now."),
                            this);
    intro->setWordWrap(true);
    ui::setTone(intro, ui::Tone::Secondary);
    layout->addWidget(intro);

    // How fragile the whole function is (#77): the busiest ones break more easily.
    function_note = new QLabel(this);
    function_note->setObjectName("function_note");
    function_note->setWordWrap(true);
    layout->addWidget(function_note);

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
    table->setHorizontalHeaderLabels({ tr("Address"), tr("Instruction"), tr("Can Become"), tr("Risk"), tr("Now") });
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(Now, QHeaderView::Stretch);
    table->horizontalHeader()->setSortIndicator(Address, Qt::AscendingOrder); // code order until a header is clicked
    table->horizontalHeaderItem(InstructionCol)->setToolTip(tr("Hover an instruction to see what it does, in plain words"));
    table->horizontalHeaderItem(Becomes)->setToolTip(tr("Hover to see what that kind of change does to the game"));
    table->horizontalHeaderItem(RiskCol)->setToolTip(tr("How likely a change here is to stop the game, from what the "
                                                         "changed value goes into next. Click a header to sort."));
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
    // Why the selected line is as risky as it is (#77), always in view, not in a tooltip.
    risk_note = new QLabel(editor);
    risk_note->setObjectName("risk_note");
    risk_note->setWordWrap(true);
    risk_note->setTextFormat(Qt::RichText);
    editor_layout->addWidget(risk_note);
    // What trying it ahead did (#78), right under the risk: the risk is the average, this is
    // this change at this moment.
    try_line = new ui::StatusLine(editor);
    try_line->setObjectName("try_line");
    try_line->hide();
    editor_layout->addWidget(try_line);

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
    try_button = new QPushButton(ui::icon("eye"), tr("Try 5 s Ahead"), editor);
    try_button->setObjectName("try");
    try_button->setAutoDefault(false);
    try_button->setToolTip(tr("Plays this change 5 seconds ahead, unseen, and once without it, then says whether the "
                              "game kept running, froze or crashed. Your game doesn't change, and pauses for a moment."));
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
    apply_row->addWidget(try_button);
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
    connect(try_button, &QPushButton::clicked, this, &FunctionEditor::tryAhead);
    connect(apply_button, &QPushButton::clicked, this, &FunctionEditor::apply);
    // Another choice: the try shown was for the old one.
    connect(number, &QSpinBox::valueChanged, this, &FunctionEditor::forgetTry);
    connect(swap_math, &QCheckBox::toggled, this, &FunctionEditor::forgetTry);
    connect(branch, &QComboBox::currentIndexChanged, this, &FunctionEditor::forgetTry);
    connect(call, &QComboBox::currentIndexChanged, this, &FunctionEditor::forgetTry);
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
    feeds = source.changeFeeds(function.f.entry);
    {
        size_t rank = 0;
        auto all = source.functions(); // busiest first
        while (rank < all.size() && all[rank].f.entry != function.f.entry)
            rank++;
        bool busy = rank < all.size() && rank < kBusiestFragile;
        function_note->setVisible(busy);
        if (busy)
            function_note->setText(tr("This is the game's busiest function number %1. In tests, changes in its 16 "
                                      "busiest functions stopped the game about %2 in 100 times, against %3 in 100 "
                                      "further down the list: they run every frame, often the main loop itself. "
                                      "The Risk column is about each line; this is about the whole function.")
                                       .arg(rank + 1)
                                       .arg(kBusiestFailures)
                                       .arg(kOthersFailures));
        ui::setTone(function_note, ui::Tone::Warning);
    }
    if (feeds.size() != options.size())
        feeds.assign(options.size(), FeedReading{});
    table->setSortingEnabled(false);
    for (auto &r : source.functions())
        if (r.f.entry == function.f.entry)
            function = r;
    table->setRowCount(int(options.size()));
    QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for (size_t i = 0; i < options.size(); i++)
    {
        auto &o = options[i];
        auto set = [&](int col, const QString &text, bool mono) {
            auto *it = new KeyedItem(text, qint64(o.at));
            if (mono)
                it->setFont(fixed);
            table->setItem(int(i), col, it);
        };
        set(Address, cpu(o.address, o.at), true);
        set(InstructionCol, QString::fromStdString(o.text), true);
        set(Becomes, becomes(o), false);
        // What it means, for players who don't read assembly (#71).
        table->item(int(i), InstructionCol)->setToolTip(QString::fromStdString(explain_instruction(o.opcode, o.text)));
        table->item(int(i), Becomes)->setToolTip(QString::fromStdString(explain_change(o.kind)));
        set(Now, QString(), false);
        table->item(int(i), Address)->setData(Qt::UserRole, int(i)); // the option, however the rows are sorted
        auto risk = risk_of(feeds[i].feed);
        auto *r = new KeyedItem(risk_word(risk) + " · " + feed_words(feeds[i].feed), (qint64(risk) << 32) | o.at);
        r->setForeground(ui::toneColor(risk_tone(risk), table->palette()));
        r->setToolTip(risk_story(feeds[i]));
        table->setItem(int(i), RiskCol, r);
    }
    table->setSortingEnabled(true);
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
    for (int row = 0; row < table->rowCount(); row++)
    {
        int i = optionAt(row);
        if (i < 0)
            continue;
        auto &o = options[size_t(i)];
        auto now = source.romNow(o.at, o.length);
        auto clean = source.romClean(o.at, o.length);
        QStringList parts;
        if (now != clean)
            parts << tr("now %1").arg(text_of(o, now));
        if (auto *k = kept(o.at))
            parts << (k->hotkey ? tr("kept, button %1").arg(k->hotkey) : tr("kept")) +
                         (k->start_on ? tr(", starts on") : QString()) +
                         (k->every ? tr(", on %1 s every %2 s").arg(k->for_frames / 60.0).arg(k->every / 60.0) : QString());
        if (auto *it = table->item(row, Now); it && it->text() != parts.join(" · "))
            it->setText(parts.join(" · "));
    }
    not_running->setVisible(!source.running());
    try_button->setEnabled(!try_after && source.running());
    if (try_after)
        if (auto t = source.lastTry(); t && t->id > *try_after)
        {
            try_after.reset();
            try_button->setEnabled(source.running());
            showTry(*t);
        }
}

void FunctionEditor::tryAhead()
{
    QString why;
    auto c = wanted(why);
    if (!c)
        return say(tr("Nothing to try: %1.").arg(why), int(ui::Tone::Warning));
    auto before = source.lastTry();
    auto err = source.tryChange(*c);
    if (!err.empty())
        return say(tr("Couldn't try it: %1").arg(QString::fromStdString(err)), int(ui::Tone::Error));
    try_after = before ? before->id : 0;
    try_shown = *c;
    try_button->setEnabled(false);
    try_line->setMessage(tr("Trying it 5 s ahead… (it runs while the game runs)"), ui::Tone::Secondary);
    try_line->show();
    timer->start(); // the answer comes at the next frame
    QTimer::singleShot(100, this, &FunctionEditor::update);
}

void FunctionEditor::showTry(const TriedChange &t)
{
    QString why;
    auto c = wanted(why);
    if (!c || c->at != t.change.at || c->value != t.change.value)
    {
        try_line->hide(); // another line or choice since: not this one's answer
        try_shown.reset();
        return;
    }
    using K = TryResult::Kind;
    auto tone = t.result.kind == K::Froze || t.result.kind == K::Crashed ? ui::Tone::Error
                : t.result.kind == K::Running                            ? ui::Tone::Ok
                : t.result.kind == K::Failed                             ? ui::Tone::Warning
                                                                         : ui::Tone::Normal;
    QString text = tr("Tried 5 s ahead: %1").arg(QString::fromStdString(t.result.text));
    if (t.result.kind == K::Running)
        text += tr(" That's for this moment of the game; later, or somewhere else, it may do more.");
    try_line->setMessage(text, tone);
    try_line->show();
    try_shown = t.change;
}

void FunctionEditor::forgetTry()
{
    if (try_after)
        return; // the answer decides when it comes
    QString why;
    auto c = wanted(why);
    if (try_shown && c && c->at == try_shown->at && c->value == try_shown->value)
        return; // still the choice it answers for
    try_line->hide();
    try_shown.reset();
}

int FunctionEditor::optionAt(int row) const
{
    auto *it = table->item(row, Address);
    if (!it)
        return -1;
    int i = it->data(Qt::UserRole).toInt();
    return i >= 0 && size_t(i) < options.size() ? i : -1;
}

int FunctionEditor::selectedRow() const
{
    auto rows = table->selectionModel() ? table->selectionModel()->selectedRows() : QModelIndexList{};
    if (rows.isEmpty())
        return -1;
    return optionAt(rows.front().row());
}

void FunctionEditor::selectionChanged()
{
    int row = selectedRow();
    editor->setVisible(row >= 0);
    if (row < 0)
        return;
    auto &o = options[size_t(row)];
    auto *k = kept(o.at);
    risk_note->setText(risk_story(feeds[size_t(row)]));
    forgetTry();
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
