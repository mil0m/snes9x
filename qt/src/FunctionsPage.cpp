#include "FunctionsPage.hpp"
#include "FunctionEditor.hpp"
#include "UiKit.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QShowEvent>
#include <QStyle>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <optional>
#include <unordered_map>

#include <cmath>

using namespace corrupt;

namespace
{

enum Col
{
    FunctionCol,
    Now,
    Frames,
    Size,
    Off,
    About,  // the names: yours first, then others' (editable: yours)
    InPlay, // what you did with it
    Cols
};

QString cpu(const FunctionRow &r)
{
    if (!r.address)
        return QString::fromStdString(hex_address(r.f.entry, Target::ROM));
    return QString("$%1:%2")
        .arg(*r.address >> 16, 2, 16, QChar('0'))
        .arg(*r.address & 0xFFFF, 4, 16, QChar('0'))
        .toUpper();
}

QString how_called(const FunctionRow &r)
{
    QString offset = QString::fromStdString(hex_address(r.f.entry, Target::ROM));
    QString callers = r.f.callers == 1 ? QObject::tr("Called from 1 place")
                                       : QObject::tr("Called from %1 places").arg(r.f.callers);
    switch (r.f.type)
    {
    case CallType::Near:
        return QObject::tr("Called near (JSR): turned off with RTS. %1. ROM offset %2.").arg(callers, offset);
    case CallType::Long:
        return QObject::tr("Called long (JSL): turned off with RTL. %1. ROM offset %2.").arg(callers, offset);
    case CallType::Mixed:
        break;
    }
    return QObject::tr("Called both near and long, so which return would skip it isn't known: it can't be turned "
                       "off safely. ROM offset %1.")
        .arg(offset);
}

// The Name column: its names, then what you do with it.
QString in_play(const FunctionRow &r)
{
    QStringList parts;
    if (r.kept)
    {
        parts << (r.kept->hotkey ? QObject::tr("button %1").arg(r.kept->hotkey) : QObject::tr("switch in the recipe"));
        if (r.kept->start_off)
            parts << QObject::tr("starts off");
        if (r.kept->every)
            parts << QObject::tr("off %1 s every %2 s").arg(r.kept->for_frames / 60.0).arg(r.kept->every / 60.0);
    }
    if (!r.changes.empty())
        parts << (r.changes.size() == 1 ? QObject::tr("1 change") : QObject::tr("%1 changes").arg(r.changes.size()));
    if (r.protect)
        parts << (r.protect->deep ? QObject::tr("protected with what it calls") : QObject::tr("protected"));
    else if (r.guarded)
        parts << QObject::tr("guarded");
    return parts.join(" · ");
}

// The Name column (#67): everyone's names, and yours to type over. The editor starts from your
// own name (the only one you change); an empty name on the selected row says how to give one.
class NameDelegate : public QStyledItemDelegate
{
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    std::function<void(int row, const QString &name)> onRename;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        auto editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (auto line = qobject_cast<QLineEdit *>(editor))
            line->setPlaceholderText(QObject::tr("What it does, like \"gravity\""));
        return editor;
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if (auto line = qobject_cast<QLineEdit *>(editor))
            line->setText(index.data(Qt::UserRole).toString());
    }
    void setModelData(QWidget *editor, QAbstractItemModel *, const QModelIndex &index) const override
    {
        // The cell shows everyone's names, so it isn't written here: the rename refreshes it.
        if (auto line = qobject_cast<QLineEdit *>(editor); line && onRename)
            onRename(index.row(), line->text().trimmed());
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);
        if (!(option.state & QStyle::State_Selected) || !index.data(Qt::DisplayRole).toString().isEmpty())
            return;
        painter->save();
        // The selection's own text colour, active or not (the Mac greys an inactive selection).
        auto group = option.state & QStyle::State_Active ? QPalette::Active : QPalette::Inactive;
        QColor hint = option.palette.color(group, QPalette::HighlightedText);
        hint.setAlphaF(0.7f);
        painter->setPen(hint);
        painter->drawText(option.rect.adjusted(6, 0, -6, 0), Qt::AlignLeft | Qt::AlignVCenter,
                          QObject::tr("Double-click to name it"));
        painter->restore();
    }
};

QString size_text(const FunctionRow &r)
{
    if (!r.body_bytes)
        return QString::fromUtf8("–");
    return QObject::tr("%1 B").arg(r.body_bytes);
}

QString size_tip(const FunctionRow &r)
{
    if (!r.body_bytes)
        return QObject::tr("Its code isn't known yet: the code map hasn't seen it run");
    QString text = r.runs.size() == 1 ? QObject::tr("Its code: %1 bytes in one piece").arg(r.body_bytes)
                                      : QObject::tr("Its code: %1 bytes in %2 pieces").arg(r.body_bytes).arg(r.runs.size());
    if (r.open_ended)
        text += QObject::tr(", and it goes on through a table of routines (not counted)");
    return text + ".";
}

} // namespace

FunctionsPage::FunctionsPage(FunctionSource &source, QWidget *parent) : QWidget(parent), source(source)
{
    setWindowTitle(tr("Functions"));
    auto layout = new QVBoxLayout(this);

    layout->addWidget(ui::heading(tr("Functions"), this));
    auto intro = new QLabel(tr("The game's own functions, as it calls them, busiest right now first. Tick <b>Off</b> "
                               "and the game skips that function: it returns as soon as it's called. Gravity, "
                               "collision, enemy AI, the scroll: try the busy ones and watch what stops. Untick to "
                               "put it back. If one breaks the game, the crash guard turns it back on."),
                            this);
    intro->setWordWrap(true);
    ui::setTone(intro, ui::Tone::Secondary);
    layout->addWidget(intro);

    not_running = new ui::StatusLine(this);
    not_running->setObjectName("not_running");
    not_running->setMessage(tr("Switches work while corruption is on: press Corrupt (F7) in the panel first."),
                            ui::Tone::Warning);
    layout->addWidget(not_running);

    empty_label = new QLabel(tr("No functions yet. Play for a moment: the game's functions show up here as it "
                                "calls them, and more as you reach new places."),
                             this);
    empty_label->setObjectName("empty");
    empty_label->setWordWrap(true);
    layout->addWidget(empty_label);

    filter = new QLineEdit(this);
    filter->setObjectName("filter");
    filter->setPlaceholderText(tr("Find by address or name"));
    filter->setAccessibleName(tr("Find a function"));
    layout->addWidget(filter);

    table = new QTableWidget(0, Cols, this);
    table->setObjectName("functions");
    table->setAccessibleName(tr("Functions"));
    table->setHorizontalHeaderLabels(
        { tr("Function"), tr("Now"), tr("Frames"), tr("Size"), tr("Off"), tr("Name"), tr("In play") });
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    // Only the Name cells are editable (their flags): double-click, or Return on the row (#67).
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    auto names = new NameDelegate(table);
    names->onRename = [this](int row, const QString &text) { rename(row, text); };
    table->setItemDelegateForColumn(About, names);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->setWordWrap(true);
    table->verticalHeader()->hide();
    // Sized once per refresh, not per cell: ResizeToContents measures every row again on each
    // change, which froze the app for a minute on a game with 545 functions.
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setSectionResizeMode(About, QHeaderView::Stretch);
    table->horizontalHeaderItem(Now)->setToolTip(tr("Of the last 5 seconds, how many frames it ran in"));
    table->horizontalHeaderItem(Frames)->setToolTip(tr("Frames it ran in, in all (everyone's play)"));
    table->horizontalHeaderItem(Size)->setToolTip(tr("How many bytes of its own code the code map knows"));
    table->horizontalHeaderItem(About)->setToolTip(tr("What people called it: yours first, then others' with their "
                                                      "names. Double-click to give it yours; everyone sharing the "
                                                      "library sees it."));
    table->horizontalHeaderItem(InPlay)->setToolTip(tr("What you did with it: a switch or a button, a timer, "
                                                       "changes, protected or guarded"));
    layout->addWidget(table, 1);

    // The selected function.
    details = new QWidget(this);
    auto details_layout = new QVBoxLayout(details);
    details_layout->setContentsMargins(0, 0, 0, 0);
    selected_label = ui::heading(QString(), details);
    selected_label->setObjectName("selected");
    details_layout->addWidget(selected_label);
    auto form = ui::form();
    details_layout->addLayout(form);

    name = new QLineEdit(details);
    name->setObjectName("name");
    name->setPlaceholderText(tr("What it does, like \"gravity\""));
    name->setToolTip(tr("Your name for it. Saved in the library (names.<you>.json), so everyone who shares it sees "
                        "it next to their own names."));
    form->addRow(tr("&Name"), name);

    auto guard_row = new QWidget(details);
    auto guard_layout = new QHBoxLayout(guard_row);
    guard_layout->setContentsMargins(0, 0, 0, 0);
    protect = new QCheckBox(tr("Protect it from other corruption"), guard_row);
    protect->setObjectName("protect");
    protect->setToolTip(tr("No rule and no paint changes its code. Switches and your own changes still work. "
                           "Kept in the recipe."));
    deep = new QCheckBox(tr("And what it calls"), guard_row);
    deep->setObjectName("deep");
    deep->setToolTip(tr("Also protect the functions it calls, and theirs (up to 32). Shared helpers can be "
                        "large: this can guard a lot of the ROM."));
    guard_layout->addWidget(protect);
    guard_layout->addWidget(deep);
    guard_layout->addStretch(1);
    form->addRow(tr("Guard"), guard_row);

    auto switch_row = new QWidget(details);
    auto switch_layout = new QHBoxLayout(switch_row);
    switch_layout->setContentsMargins(0, 0, 0, 0);
    hotkey = new QComboBox(switch_row);
    hotkey->setObjectName("hotkey");
    hotkey->setAccessibleName(tr("Button for the switch"));
    hotkey->addItem(tr("No button"));
    for (int k = 1; k <= 9; k++)
        hotkey->addItem(tr("Button %1").arg(k));
    hotkey->setToolTip(tr("A button in the corruption panel (and on the remote page) that turns the function "
                          "off, and on again"));
    start_off = new QCheckBox(tr("Starts off"), switch_row);
    start_off->setObjectName("start_off");
    start_off->setToolTip(tr("Off from the start of every run"));
    keep_button = new QPushButton(ui::icon("pin"), tr("Keep in Recipe"), switch_row);
    keep_button->setObjectName("keep");
    keep_button->setAutoDefault(false);
    forget_button = new QPushButton(ui::icon("pinned-off"), tr("Remove"), switch_row);
    forget_button->setObjectName("forget");
    forget_button->setAutoDefault(false);
    forget_button->setToolTip(tr("Take the switch out of the recipe. The function stays as it is now."));
    switch_layout->addWidget(hotkey);
    switch_layout->addWidget(start_off);
    switch_layout->addStretch(1);
    switch_layout->addWidget(keep_button);
    switch_layout->addWidget(forget_button);
    form->addRow(tr("Switch"), switch_row);

    // A timer (#61): off for a while, again and again.
    auto timer_row = new QWidget(details);
    auto timer_layout = new QHBoxLayout(timer_row);
    timer_layout->setContentsMargins(0, 0, 0, 0);
    timer_check = new QCheckBox(tr("Off for"), timer_row);
    timer_check->setObjectName("timer");
    timer_check->setToolTip(tr("Kept with the switch: it turns off and on by itself, counted from the start of the "
                               "run. Short times make it flicker."));
    auto seconds = [&](const char *name, double value, const QString &what) {
        auto s = new QDoubleSpinBox(timer_row);
        s->setObjectName(name);
        s->setRange(0.1, 600);
        s->setDecimals(1);
        s->setSingleStep(0.5);
        s->setSuffix(tr(" s"));
        s->setValue(value);
        s->setAccessibleName(what);
        return s;
    };
    timer_for = seconds("timer_for", 2, tr("Off for how long"));
    timer_every = seconds("timer_every", 10, tr("Every how long"));
    timer_layout->addWidget(timer_check);
    timer_layout->addWidget(timer_for);
    timer_layout->addWidget(new QLabel(tr("every"), timer_row));
    timer_layout->addWidget(timer_every);
    timer_layout->addStretch(1);
    form->addRow(tr("Timer"), timer_row);

    auto actions = new QHBoxLayout;
    show_button = new QPushButton(ui::icon("eye"), tr("Show in ROM"), details);
    show_button->setObjectName("show");
    show_button->setAutoDefault(false);
    show_button->setToolTip(tr("Open the memory viewer on the ROM with its code selected and the Functions colours"));
    change_button = new QPushButton(ui::icon("adjustments-horizontal"), tr("Change It…"), details);
    change_button->setObjectName("change");
    change_button->setAutoDefault(false);
    change_button->setToolTip(tr("Change how it works instead of turning it off: its numbers, its decisions, its "
                                 "sums, the functions it calls"));
    actions->addWidget(change_button);
    actions->addWidget(show_button);
    actions->addStretch(1);
    details_layout->addLayout(actions);
    layout->addWidget(details);

    message = new ui::StatusLine(this);
    message->setObjectName("message");
    layout->addWidget(message);
    layout->addStretch(0); // takes the slack only when the table is hidden


    timer = new QTimer(this);
    timer->setInterval(500);
    connect(timer, &QTimer::timeout, this, &FunctionsPage::updateRows);
    connect(table, &QTableWidget::itemChanged, this, &FunctionsPage::offChanged);
    connect(table, &QTableWidget::itemSelectionChanged, this, &FunctionsPage::selectionChanged);
    connect(table->horizontalHeader(), &QHeaderView::sectionResized, table, [this](int col) {
        if (col == About)
            table->resizeRowsToContents();
    });
    connect(filter, &QLineEdit::textChanged, this, &FunctionsPage::applyFilter);
    connect(keep_button, &QPushButton::clicked, this, &FunctionsPage::keepSelected);
    connect(forget_button, &QPushButton::clicked, this, &FunctionsPage::forgetSelected);
    connect(name, &QLineEdit::editingFinished, this, &FunctionsPage::nameSelected);
    connect(protect, &QCheckBox::toggled, this, &FunctionsPage::protectChanged);
    connect(deep, &QCheckBox::toggled, this, &FunctionsPage::protectChanged);
    connect(show_button, &QPushButton::clicked, this, &FunctionsPage::showSelected);
    connect(change_button, &QPushButton::clicked, this, &FunctionsPage::changeSelected);
    message->hide();
    refresh();
}

void FunctionsPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!event->spontaneous())
        refresh(); // busiest right now first again, with any new functions; never under the mouse
    timer->start();
}

void FunctionsPage::hideEvent(QHideEvent *event)
{
    timer->stop();
    QWidget::hideEvent(event);
}

void FunctionsPage::say(const QString &text, int tone)
{
    message->setMessage(text, ui::Tone(tone));
    message->show();
}

void FunctionsPage::fill(int row, const FunctionRow &r)
{
    filling = true;
    auto item = [&](int col) {
        auto *it = table->item(row, col);
        if (!it)
        {
            it = new QTableWidgetItem;
            it->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled); // only names are typed in (below)
            table->setItem(row, col, it);
        }
        return it;
    };
    // Only what changed: every set is a repaint, and this runs twice a second for every row.
    auto set = [](QTableWidgetItem *it, const QString &text, const QString &tip) {
        if (it->text() != text)
            it->setText(text);
        if (it->toolTip() != tip)
            it->setToolTip(tip);
    };
    auto *fn = item(FunctionCol);
    if (fn->text().isEmpty())
        fn->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    set(fn, cpu(r), how_called(r));
    auto *now = item(Now);
    if (now->text().isEmpty())
        now->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    set(now, QString("%1%").arg(r.f.now * 100 / CallKeeper::kNowFrames), {});
    auto *frames = item(Frames);
    if (frames->text().isEmpty())
        frames->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    set(frames, QLocale().toString(qulonglong(r.f.frames)),
        r.f.callers == 1 ? tr("Called from 1 place") : tr("Called from %1 places").arg(r.f.callers));
    auto *size = item(Size);
    if (size->text().isEmpty())
        size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    set(size, size_text(r), size_tip(r));
    auto *off = item(Off);
    if (r.f.type == CallType::Mixed)
    {
        off->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        off->setData(Qt::CheckStateRole, QVariant());
        off->setToolTip(how_called(r));
    }
    else
    {
        auto flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
        if (off->flags() != flags)
            off->setFlags(flags);
        auto state = r.off ? Qt::Checked : Qt::Unchecked;
        if (off->data(Qt::CheckStateRole).isNull() || off->checkState() != state)
            off->setCheckState(state);
        QString tip = r.off ? tr("Off: the game skips it. Untick to put it back.")
                            : tr("Tick to turn it off: the game skips it");
        if (off->toolTip() != tip)
            off->setToolTip(tip);
    }
    auto *ab = item(About);
    auto name_flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
    if (ab->flags() != name_flags)
        ab->setFlags(name_flags);
    QString label = QString::fromStdString(r.label), mine = QString::fromStdString(r.my_name);
    if (ab->text() != label)
        ab->setText(label);
    if (ab->data(Qt::UserRole).toString() != mine)
        ab->setData(Qt::UserRole, mine);
    auto *state = item(InPlay);
    QString text = in_play(r);
    if (state->text() != text)
    {
        state->setText(text);
        in_play_grew = in_play_grew || table->fontMetrics().horizontalAdvance(text) + 16 > table->columnWidth(InPlay);
    }
    filling = false;
}

void FunctionsPage::refresh()
{
    std::optional<uint32_t> selected;
    if (int row = selectedRow(); row >= 0)
        selected = shown[size_t(row)].f.entry; // still selected after the re-sort
    shown = source.functions();
    filling = true;
    table->setRowCount(int(shown.size()));
    filling = false;
    for (size_t i = 0; i < shown.size(); i++)
        fill(int(i), shown[i]);
    for (int col : { int(FunctionCol), int(Now), int(Frames), int(Size), int(Off), int(InPlay) })
        table->resizeColumnToContents(col);
    // Frames keeps counting while the page is shown: room for 46 hours of play, so the
    // numbers never outgrow the column between re-sorts.
    int frames_room = table->fontMetrics().horizontalAdvance(QLocale().toString(9999999)) +
                      2 * table->style()->pixelMetric(QStyle::PM_FocusFrameHMargin) + 12;
    if (table->columnWidth(Frames) < frames_room)
        table->setColumnWidth(Frames, frames_room);
    table->resizeRowsToContents();
    bool any = !shown.empty();
    table->setVisible(any);
    filter->setVisible(any);
    empty_label->setVisible(!any);
    not_running->setVisible(!source.running());
    applyFilter();
    if (selected)
        for (size_t i = 0; i < shown.size(); i++)
            if (shown[i].f.entry == *selected)
            {
                table->selectRow(int(i));
                table->scrollToItem(table->item(int(i), 0));
            }
    selectionChanged();
}

void FunctionsPage::updateRows()
{
    auto now = source.functions();
    std::unordered_map<uint32_t, const FunctionRow *> by_entry;
    for (auto &r : now)
        by_entry[r.f.entry] = &r;
    for (size_t i = 0; i < shown.size(); i++)
        if (auto it = by_entry.find(shown[i].f.entry); it != by_entry.end())
        {
            shown[i] = *it->second;
            fill(int(i), shown[i]);
        }
    if (in_play_grew) // a switch kept or a function protected since the last re-sort
    {
        in_play_grew = false;
        table->resizeColumnToContents(InPlay);
    }
    not_running->setVisible(!source.running());
}

int FunctionsPage::selectedRow() const
{
    auto rows = table->selectionModel() ? table->selectionModel()->selectedRows() : QModelIndexList{};
    if (rows.isEmpty() || size_t(rows.front().row()) >= shown.size())
        return -1;
    return rows.front().row();
}

void FunctionsPage::offChanged(QTableWidgetItem *item)
{
    if (filling || item->column() != Off || size_t(item->row()) >= shown.size())
        return;
    auto &r = shown[size_t(item->row())];
    bool want_off = item->checkState() == Qt::Checked;
    if (want_off == r.off)
        return;
    if (!source.running())
    {
        fill(item->row(), r); // back as it was
        not_running->show();
        return;
    }
    auto err = source.flip(r.f.entry);
    if (!err.empty())
    {
        fill(item->row(), r);
        say(tr("Couldn't switch it: %1").arg(QString::fromStdString(err)), int(ui::Tone::Error));
    }
    else
    {
        r.off = want_off; // what it will be after the next frame; the timer reads it back
        say(want_off ? tr("Turned %1 off; its code is guarded from other corruption while it's off. Watch the "
                          "game: if nothing changes, try the next one.")
                           .arg(cpu(r))
                     : tr("Turned %1 back on.").arg(cpu(r)));
    }
}

void FunctionsPage::selectionChanged()
{
    int row = selectedRow();
    details->setVisible(row >= 0);
    if (row < 0)
        return;
    auto &r = shown[size_t(row)];
    selecting = true;
    QString label = QString::fromStdString(r.label);
    selected_label->setText(label.isEmpty() ? cpu(r) : cpu(r) + " · " + label);
    name->setText(QString::fromStdString(r.my_name));
    protect->setChecked(r.protect.has_value());
    deep->setChecked(r.protect && r.protect->deep);
    deep->setEnabled(r.protect.has_value());
    protect->setEnabled(r.body_bytes > 0);
    bool can_switch = r.f.type != CallType::Mixed;
    keep_button->setEnabled(can_switch);
    forget_button->setEnabled(r.kept.has_value());
    hotkey->setCurrentIndex(r.kept ? r.kept->hotkey : 0);
    start_off->setChecked(r.kept && r.kept->start_off);
    timer_check->setChecked(r.kept && r.kept->every);
    if (r.kept && r.kept->every)
    {
        timer_for->setValue(r.kept->for_frames / 60.0);
        timer_every->setValue(r.kept->every / 60.0);
    }
    show_button->setEnabled(!r.runs.empty());
    change_button->setEnabled(r.body_bytes > 0);
    selecting = false;
}

void FunctionsPage::applyFilter()
{
    QString want = filter->text().trimmed().toLower().remove('$').remove(':');
    for (size_t i = 0; i < shown.size(); i++)
    {
        auto &r = shown[i];
        QString fn = cpu(r).toLower().remove('$').remove(':');
        QString offset = QString::fromStdString(hex_address(r.f.entry, Target::ROM)).toLower();
        QString text = QString::fromStdString(r.label).toLower();
        if (r.kept)
            text += " " + QString::fromStdString(r.kept->note).toLower();
        bool match = want.isEmpty() || fn.contains(want) || offset.contains(want) || text.contains(want);
        table->setRowHidden(int(i), !match);
    }
}

void FunctionsPage::nameSelected()
{
    int row = selectedRow();
    if (row < 0 || selecting)
        return;
    rename(row, name->text().trimmed());
}

void FunctionsPage::rename(int row, const QString &text)
{
    if (row < 0 || size_t(row) >= shown.size())
        return;
    auto &r = shown[size_t(row)];
    std::string want = text.toStdString();
    if (want == r.my_name)
        return;
    auto err = source.setName(r.f.entry, want);
    if (!err.empty())
        say(tr("Couldn't save the name: %1").arg(QString::fromStdString(err)), int(ui::Tone::Error));
    else
        say(want.empty() ? tr("Name removed.")
                         : tr("Named %1 \"%2\": everyone who shares the library sees it.").arg(cpu(r), text),
            int(ui::Tone::Ok));
    updateRows();
    if (row == selectedRow() && name->text().trimmed() != text)
    {
        QSignalBlocker block(name);
        name->setText(text); // the field under the table agrees with the cell
    }
    table->resizeRowsToContents();
}

void FunctionsPage::protectChanged()
{
    int row = selectedRow();
    if (row < 0 || selecting)
        return;
    auto &r = shown[size_t(row)];
    bool on = protect->isChecked();
    deep->setEnabled(on);
    auto err = source.protect(r.f.entry, on, on && deep->isChecked());
    if (!err.empty())
    {
        say(tr("Couldn't protect it: %1").arg(QString::fromStdString(err)), int(ui::Tone::Error));
        selecting = true;
        protect->setChecked(r.protect.has_value());
        selecting = false;
        return;
    }
    say(on ? tr("Protected: no rule and no paint changes its code now. Save the recipe to keep it.")
           : tr("Not protected any more."),
        int(ui::Tone::Ok));
    updateRows();
}

void FunctionsPage::keepSelected()
{
    int row = selectedRow();
    if (row < 0)
        return;
    auto &r = shown[size_t(row)];
    std::string note = name->text().trimmed().toStdString();
    if (note.empty())
        note = r.label;
    uint32_t every = 0, for_frames = 0;
    if (timer_check->isChecked())
    {
        every = uint32_t(std::lround(timer_every->value() * 60));
        for_frames = uint32_t(std::lround(timer_for->value() * 60));
        if (for_frames >= every)
        {
            say(tr("The timer's off time has to be shorter than its cycle."), int(ui::Tone::Warning));
            return;
        }
    }
    auto err = source.keep(r.f.entry, hotkey->currentIndex(), start_off->isChecked(), note, every, for_frames);
    if (!err.empty())
        say(tr("Couldn't keep it: %1").arg(QString::fromStdString(err)), int(ui::Tone::Error));
    else
        say(hotkey->currentIndex()
                ? tr("Kept in the recipe on button %1. Save the recipe to keep it.").arg(hotkey->currentIndex())
                : tr("Kept in the recipe. Save the recipe to keep it."),
            int(ui::Tone::Ok));
    updateRows();
    selectionChanged();
}

void FunctionsPage::forgetSelected()
{
    int row = selectedRow();
    if (row < 0)
        return;
    source.forget(shown[size_t(row)].f.entry);
    say(tr("Removed from the recipe. The function stays as it is now."));
    updateRows();
    selectionChanged();
}

void FunctionsPage::changeSelected()
{
    int row = selectedRow();
    if (row < 0)
        return;
    auto editor = new FunctionEditor(source, shown[size_t(row)], this);
    editor->setAttribute(Qt::WA_DeleteOnClose);
    editor->show();
}

void FunctionsPage::showSelected()
{
    int row = selectedRow();
    if (row < 0 || shown[size_t(row)].runs.empty())
        return;
    auto &r = shown[size_t(row)];
    // The piece holding the entry: a body's pieces can be far apart.
    auto piece = r.runs.front();
    for (auto &p : r.runs)
        if (r.f.entry >= p.first && r.f.entry <= p.second)
            piece = p;
    if (onShowInRom)
        onShowInRom(piece.first, piece.second);
}
