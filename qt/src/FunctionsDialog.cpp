#include "FunctionsDialog.hpp"
#include "UiKit.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

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
    About,
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
QString about(const FunctionRow &r)
{
    QStringList parts;
    if (r.kept)
    {
        parts << (r.kept->hotkey ? QObject::tr("key %1").arg(r.kept->hotkey) : QObject::tr("switch in the recipe"));
        if (r.kept->start_off)
            parts << QObject::tr("starts off");
    }
    if (r.protect)
        parts << (r.protect->deep ? QObject::tr("protected with what it calls") : QObject::tr("protected"));
    else if (r.guarded)
        parts << QObject::tr("guarded");
    QString state = parts.join(" · ");
    QString label = QString::fromStdString(r.label);
    if (label.isEmpty())
        return state;
    return state.isEmpty() ? label : label + QString::fromUtf8(" — ") + state;
}

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

FunctionsDialog::FunctionsDialog(FunctionSource &source, QWidget *parent) : QDialog(parent), source(source)
{
    setWindowTitle(tr("Functions"));
    resize(720, 640);
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
    table->setHorizontalHeaderLabels({ tr("Function"), tr("Now"), tr("Frames"), tr("Size"), tr("Off"), tr("Name") });
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->setWordWrap(true);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(About, QHeaderView::Stretch);
    table->horizontalHeaderItem(Now)->setToolTip(tr("Of the last 5 seconds, how many frames it ran in"));
    table->horizontalHeaderItem(Frames)->setToolTip(tr("Frames it ran in, in all (everyone's play)"));
    table->horizontalHeaderItem(Size)->setToolTip(tr("How many bytes of its own code the code map knows"));
    table->horizontalHeaderItem(About)->setToolTip(tr("What people called it (yours first, then others' with "
                                                      "their names), and what you do with it"));
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
    hotkey->setAccessibleName(tr("Hotkey for the switch"));
    hotkey->addItem(tr("No hotkey"));
    for (int k = 1; k <= 9; k++)
        hotkey->addItem(tr("Key %1").arg(k));
    hotkey->setToolTip(tr("Pressing this hotkey (1-9 in the panel, the keyboard or the remote page) turns the "
                          "function off, and on again"));
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

    auto actions = new QHBoxLayout;
    show_button = new QPushButton(ui::icon("eye"), tr("Show in ROM"), details);
    show_button->setObjectName("show");
    show_button->setAutoDefault(false);
    show_button->setToolTip(tr("Open the memory viewer on the ROM with its code selected and the Functions colours"));
    actions->addWidget(show_button);
    actions->addStretch(1);
    details_layout->addLayout(actions);
    layout->addWidget(details);

    message = new ui::StatusLine(this);
    message->setObjectName("message");
    layout->addWidget(message);
    layout->addStretch(0); // takes the slack only when the table is hidden

    auto bottom = new QHBoxLayout;
    auto refresh_button = new QPushButton(ui::icon("refresh"), tr("Refresh"), this);
    refresh_button->setObjectName("refresh");
    refresh_button->setAutoDefault(false);
    refresh_button->setToolTip(tr("Busiest right now first again, with any new functions"));
    auto close_button = new QPushButton(tr("Close"), this);
    close_button->setAutoDefault(false);
    bottom->addStretch(1);
    bottom->addWidget(refresh_button);
    bottom->addWidget(close_button);
    layout->addLayout(bottom);

    timer = new QTimer(this);
    timer->setInterval(500);
    connect(timer, &QTimer::timeout, this, &FunctionsDialog::update);
    connect(refresh_button, &QPushButton::clicked, this, &FunctionsDialog::refresh);
    connect(close_button, &QPushButton::clicked, this, &QDialog::close);
    connect(table, &QTableWidget::itemChanged, this, &FunctionsDialog::offChanged);
    connect(table, &QTableWidget::itemSelectionChanged, this, &FunctionsDialog::selectionChanged);
    connect(table->horizontalHeader(), &QHeaderView::sectionResized, table, [this](int col) {
        if (col == About)
            table->resizeRowsToContents();
    });
    connect(filter, &QLineEdit::textChanged, this, &FunctionsDialog::applyFilter);
    connect(keep_button, &QPushButton::clicked, this, &FunctionsDialog::keepSelected);
    connect(forget_button, &QPushButton::clicked, this, &FunctionsDialog::forgetSelected);
    connect(name, &QLineEdit::editingFinished, this, &FunctionsDialog::nameSelected);
    connect(protect, &QCheckBox::toggled, this, &FunctionsDialog::protectChanged);
    connect(deep, &QCheckBox::toggled, this, &FunctionsDialog::protectChanged);
    connect(show_button, &QPushButton::clicked, this, &FunctionsDialog::showSelected);
    message->hide();
    refresh();
}

void FunctionsDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    timer->start();
}

void FunctionsDialog::hideEvent(QHideEvent *event)
{
    timer->stop();
    QDialog::hideEvent(event);
}

void FunctionsDialog::say(const QString &text, int tone)
{
    message->setMessage(text, ui::Tone(tone));
    message->show();
}

void FunctionsDialog::fill(int row, const FunctionRow &r)
{
    filling = true;
    auto item = [&](int col) {
        auto *it = table->item(row, col);
        if (!it)
        {
            it = new QTableWidgetItem;
            table->setItem(row, col, it);
        }
        return it;
    };
    QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    auto *fn = item(FunctionCol);
    fn->setText(cpu(r));
    fn->setFont(fixed);
    fn->setToolTip(how_called(r));
    auto *now = item(Now);
    now->setText(QString("%1%").arg(r.f.now * 100 / CallKeeper::kNowFrames));
    now->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *frames = item(Frames);
    frames->setText(QLocale().toString(qulonglong(r.f.frames)));
    frames->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    frames->setToolTip(r.f.callers == 1 ? tr("Called from 1 place") : tr("Called from %1 places").arg(r.f.callers));
    auto *size = item(Size);
    size->setText(size_text(r));
    size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    size->setToolTip(size_tip(r));
    auto *off = item(Off);
    if (r.f.type == CallType::Mixed)
    {
        off->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        off->setData(Qt::CheckStateRole, QVariant());
        off->setToolTip(how_called(r));
    }
    else
    {
        off->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        off->setCheckState(r.off ? Qt::Checked : Qt::Unchecked);
        off->setToolTip(r.off ? tr("Off: the game skips it. Untick to put it back.")
                              : tr("Tick to turn it off: the game skips it"));
    }
    auto *ab = item(About);
    QString text = about(r);
    if (ab->text() != text)
        ab->setText(text);
    filling = false;
}

void FunctionsDialog::refresh()
{
    shown = source.functions();
    filling = true;
    table->setRowCount(int(shown.size()));
    filling = false;
    for (size_t i = 0; i < shown.size(); i++)
        fill(int(i), shown[i]);
    table->resizeRowsToContents();
    bool any = !shown.empty();
    table->setVisible(any);
    filter->setVisible(any);
    empty_label->setVisible(!any);
    not_running->setVisible(!source.running());
    applyFilter();
    selectionChanged();
}

void FunctionsDialog::update()
{
    auto now = source.functions();
    for (size_t i = 0; i < shown.size(); i++)
        for (auto &r : now)
            if (r.f.entry == shown[i].f.entry)
            {
                shown[i] = r;
                fill(int(i), r);
                break;
            }
    not_running->setVisible(!source.running());
}

int FunctionsDialog::selectedRow() const
{
    auto rows = table->selectionModel() ? table->selectionModel()->selectedRows() : QModelIndexList{};
    if (rows.isEmpty() || size_t(rows.front().row()) >= shown.size())
        return -1;
    return rows.front().row();
}

void FunctionsDialog::offChanged(QTableWidgetItem *item)
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

void FunctionsDialog::selectionChanged()
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
    show_button->setEnabled(!r.runs.empty());
    selecting = false;
}

void FunctionsDialog::applyFilter()
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

void FunctionsDialog::nameSelected()
{
    int row = selectedRow();
    if (row < 0 || selecting)
        return;
    auto &r = shown[size_t(row)];
    std::string want = name->text().trimmed().toStdString();
    if (want == r.my_name)
        return;
    auto err = source.setName(r.f.entry, want);
    if (!err.empty())
        say(tr("Couldn't save the name: %1").arg(QString::fromStdString(err)), int(ui::Tone::Error));
    else
        say(want.empty() ? tr("Name removed.")
                         : tr("Named %1 \"%2\": everyone who shares the library sees it.").arg(cpu(r), name->text().trimmed()),
            int(ui::Tone::Ok));
    update();
    table->resizeRowsToContents();
}

void FunctionsDialog::protectChanged()
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
    update();
}

void FunctionsDialog::keepSelected()
{
    int row = selectedRow();
    if (row < 0)
        return;
    auto &r = shown[size_t(row)];
    std::string note = name->text().trimmed().toStdString();
    if (note.empty())
        note = r.label;
    auto err = source.keep(r.f.entry, hotkey->currentIndex(), start_off->isChecked(), note);
    if (!err.empty())
        say(tr("Couldn't keep it: %1").arg(QString::fromStdString(err)), int(ui::Tone::Error));
    else
        say(hotkey->currentIndex()
                ? tr("Kept in the recipe on key %1. Save the recipe to keep it.").arg(hotkey->currentIndex())
                : tr("Kept in the recipe. Save the recipe to keep it."),
            int(ui::Tone::Ok));
    update();
    selectionChanged();
}

void FunctionsDialog::forgetSelected()
{
    int row = selectedRow();
    if (row < 0)
        return;
    source.forget(shown[size_t(row)].f.entry);
    say(tr("Removed from the recipe. The function stays as it is now."));
    update();
    selectionChanged();
}

void FunctionsDialog::showSelected()
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
