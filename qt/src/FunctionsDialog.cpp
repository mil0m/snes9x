#include "FunctionsDialog.hpp"
#include "UiKit.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
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
    Name,
    Now,
    Frames,
    Callers,
    Off,
    Recipe,
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

QString in_recipe(const FunctionRow &r)
{
    if (!r.kept)
        return {};
    QStringList parts;
    parts << (r.kept->hotkey ? QObject::tr("key %1").arg(r.kept->hotkey) : QObject::tr("in the recipe"));
    if (r.kept->start_off)
        parts << QObject::tr("starts off");
    if (!r.kept->note.empty())
        parts << QString::fromStdString(r.kept->note);
    return parts.join(" · ");
}

} // namespace

FunctionsDialog::FunctionsDialog(FunctionSource &source, QWidget *parent) : QDialog(parent), source(source)
{
    setWindowTitle(tr("Functions"));
    resize(680, 560);
    auto layout = new QVBoxLayout(this);

    layout->addWidget(ui::heading(tr("Functions"), this));
    auto about = new QLabel(tr("The game's own functions, as it calls them, busiest right now first. Tick <b>Off</b> "
                               "and the game skips that function: it returns as soon as it's called. Gravity, "
                               "collision, enemy AI, the scroll: try the busy ones and watch what stops. Untick to "
                               "put it back. If one breaks the game, the crash guard turns it back on."),
                            this);
    about->setWordWrap(true);
    ui::setTone(about, ui::Tone::Secondary);
    layout->addWidget(about);

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
    filter->setPlaceholderText(tr("Find by address or note"));
    filter->setAccessibleName(tr("Find a function"));
    layout->addWidget(filter);

    table = new QTableWidget(0, Cols, this);
    table->setObjectName("functions");
    table->setAccessibleName(tr("Functions"));
    table->setHorizontalHeaderLabels({ tr("Function"), tr("Now"), tr("Frames"), tr("Callers"), tr("Off"), tr("In Recipe") });
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(Recipe, QHeaderView::Stretch);
    table->horizontalHeaderItem(Now)->setToolTip(tr("Of the last 5 seconds, how many frames it ran in"));
    table->horizontalHeaderItem(Frames)->setToolTip(tr("Frames it ran in, in all (everyone's play)"));
    table->horizontalHeaderItem(Callers)->setToolTip(tr("Places in the game's code that call it"));
    layout->addWidget(table, 1);

    switch_row = new QWidget(this);
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
    note = new QLineEdit(switch_row);
    note->setObjectName("note");
    note->setPlaceholderText(tr("What it does, like \"gravity\" (optional)"));
    note->setAccessibleName(tr("Note for the switch"));
    keep_button = new QPushButton(ui::icon("pin"), tr("Keep in Recipe"), switch_row);
    keep_button->setObjectName("keep");
    keep_button->setAutoDefault(false);
    forget_button = new QPushButton(ui::icon("pinned-off"), tr("Remove"), switch_row);
    forget_button->setObjectName("forget");
    forget_button->setAutoDefault(false);
    forget_button->setToolTip(tr("Take the switch out of the recipe. The function stays as it is now."));
    switch_layout->addWidget(hotkey);
    switch_layout->addWidget(start_off);
    switch_layout->addWidget(note, 1);
    switch_layout->addWidget(keep_button);
    switch_layout->addWidget(forget_button);
    layout->addWidget(switch_row);

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
    connect(filter, &QLineEdit::textChanged, this, &FunctionsDialog::applyFilter);
    connect(keep_button, &QPushButton::clicked, this, &FunctionsDialog::keepSelected);
    connect(forget_button, &QPushButton::clicked, this, &FunctionsDialog::forgetSelected);
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
    auto *name = item(Name);
    name->setText(cpu(r));
    name->setFont(fixed);
    name->setToolTip(how_called(r));
    auto *now = item(Now);
    now->setText(QString("%1%").arg(r.f.now * 100 / CallKeeper::kNowFrames));
    now->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *frames = item(Frames);
    frames->setText(QLocale().toString(qulonglong(r.f.frames)));
    frames->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *callers = item(Callers);
    callers->setText(QString::number(r.f.callers));
    callers->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
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
    item(Recipe)->setText(in_recipe(r));
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
    bool any = !shown.empty();
    table->setVisible(any);
    filter->setVisible(any);
    switch_row->setVisible(any);
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
        message->setMessage(tr("Couldn't switch it: %1").arg(QString::fromStdString(err)), ui::Tone::Error);
    }
    else
    {
        r.off = want_off; // what it will be after the next frame; the timer reads it back
        message->setMessage(want_off ? tr("Turned %1 off. Watch the game: if nothing changes, try the next one.")
                                           .arg(cpu(r))
                                     : tr("Turned %1 back on.").arg(cpu(r)));
    }
    message->show();
}

void FunctionsDialog::selectionChanged()
{
    int row = selectedRow();
    bool can = row >= 0 && shown[size_t(row)].f.type != CallType::Mixed;
    keep_button->setEnabled(can);
    forget_button->setEnabled(row >= 0 && shown[size_t(row)].kept.has_value());
    if (row >= 0 && shown[size_t(row)].kept)
    {
        auto &k = *shown[size_t(row)].kept;
        hotkey->setCurrentIndex(k.hotkey);
        start_off->setChecked(k.start_off);
        note->setText(QString::fromStdString(k.note));
    }
}

void FunctionsDialog::applyFilter()
{
    QString want = filter->text().trimmed().toLower().remove('$').remove(':');
    for (size_t i = 0; i < shown.size(); i++)
    {
        auto &r = shown[i];
        QString name = cpu(r).toLower().remove('$').remove(':');
        QString offset = QString::fromStdString(hex_address(r.f.entry, Target::ROM)).toLower();
        QString text = r.kept ? QString::fromStdString(r.kept->note).toLower() : QString();
        bool match = want.isEmpty() || name.contains(want) || offset.contains(want) || text.contains(want);
        table->setRowHidden(int(i), !match);
    }
}

void FunctionsDialog::keepSelected()
{
    int row = selectedRow();
    if (row < 0)
        return;
    auto &r = shown[size_t(row)];
    auto err = source.keep(r.f.entry, hotkey->currentIndex(), start_off->isChecked(), note->text().trimmed().toStdString());
    if (!err.empty())
        message->setMessage(tr("Couldn't keep it: %1").arg(QString::fromStdString(err)), ui::Tone::Error);
    else
        message->setMessage(hotkey->currentIndex()
                                ? tr("Kept in the recipe on key %1. Save the recipe to keep it.").arg(hotkey->currentIndex())
                                : tr("Kept in the recipe. Save the recipe to keep it."),
                            ui::Tone::Ok);
    message->show();
    update();
    selectionChanged();
}

void FunctionsDialog::forgetSelected()
{
    int row = selectedRow();
    if (row < 0)
        return;
    source.forget(shown[size_t(row)].f.entry);
    message->setMessage(tr("Removed from the recipe. The function stays as it is now."));
    message->show();
    update();
    selectionChanged();
}
