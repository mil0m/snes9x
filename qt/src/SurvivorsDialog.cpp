#include "SurvivorsDialog.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <set>

using namespace corrupt;

namespace
{

QString hex2(uint8_t v)
{
    return QString("%1").arg(v, 2, 16, QChar('0')).toUpper();
}

QString where(uint32_t offset, std::optional<uint32_t> address)
{
    if (address)
        return QString("$%1:%2")
            .arg(*address >> 16, 2, 16, QChar('0'))
            .arg(*address & 0xFFFF, 4, 16, QChar('0'))
            .toUpper();
    return "0x" + QString::number(offset, 16).toUpper();
}

QTableWidget *make_table(QWidget *parent, const char *name, const QStringList &headers)
{
    auto t = new QTableWidget(0, int(headers.size()), parent);
    t->setObjectName(name);
    t->setHorizontalHeaderLabels(headers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::ExtendedSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->verticalHeader()->hide();
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}

std::vector<int> selected_rows(QTableWidget *t)
{
    std::set<int> rows;
    for (auto *item : t->selectedItems())
        rows.insert(item->row());
    return { rows.begin(), rows.end() };
}

} // namespace

SurvivorsDialog::SurvivorsDialog(SurvivorSource &source, QWidget *parent) : QDialog(parent), source(source)
{
    setWindowTitle(tr("Survivors and Pins"));
    resize(640, 520);
    auto layout = new QVBoxLayout(this);

    auto about = new QLabel(tr("<b>Survivors</b> are ROM changes the game has used in 100 frames or more, with no "
                               "crash in the 15 seconds after. <b>Pin</b> the ones you like to keep them in the "
                               "recipe: pins apply whenever a run starts, before the rules, and the crash guard "
                               "never undoes them. Save the recipe to keep its pins."),
                            this);
    about->setWordWrap(true);
    layout->addWidget(about);

    empty_label = new QLabel(tr("No survivors yet. Turn on a recipe that changes the ROM and play: changes the "
                                "game keeps using without crashing show up here."),
                             this);
    empty_label->setObjectName("empty");
    empty_label->setWordWrap(true);
    empty_label->setEnabled(false);
    layout->addWidget(empty_label);

    table = make_table(this, "survivors", { tr("Where"), tr("Change"), tr("Instruction"), tr("Used") });
    layout->addWidget(table, 2);

    auto pin_row = new QHBoxLayout;
    note_edit = new QLineEdit(this);
    note_edit->setObjectName("note");
    note_edit->setPlaceholderText(tr("A note for the pin, like \"doors stay shut\" (optional)"));
    pin_button = new QPushButton(tr("Pin Selected"), this);
    pin_button->setObjectName("pin");
    pin_button->setAutoDefault(false);
    pin_row->addWidget(note_edit, 1);
    pin_row->addWidget(pin_button);
    layout->addLayout(pin_row);

    message = new QLabel(this);
    message->setObjectName("message");
    message->setWordWrap(true);
    layout->addWidget(message);

    layout->addWidget(new QLabel(tr("<b>Pinned</b> in this recipe"), this));
    pins_table = make_table(this, "pins", { tr("ROM offset"), tr("Clean → kept"), tr("Note") });
    layout->addWidget(pins_table, 1);

    auto bottom = new QHBoxLayout;
    unpin_button = new QPushButton(tr("Unpin Selected"), this);
    unpin_button->setObjectName("unpin");
    unpin_button->setAutoDefault(false);
    auto refresh_button = new QPushButton(tr("Refresh"), this);
    refresh_button->setObjectName("refresh");
    refresh_button->setAutoDefault(false);
    auto close_button = new QPushButton(tr("Close"), this);
    close_button->setAutoDefault(false);
    bottom->addWidget(unpin_button);
    bottom->addStretch(1);
    bottom->addWidget(refresh_button);
    bottom->addWidget(close_button);
    layout->addLayout(bottom);

    connect(pin_button, &QPushButton::clicked, this, &SurvivorsDialog::pinSelected);
    connect(unpin_button, &QPushButton::clicked, this, &SurvivorsDialog::unpinSelected);
    connect(refresh_button, &QPushButton::clicked, this, &SurvivorsDialog::refresh);
    connect(close_button, &QPushButton::clicked, this, &QDialog::close);
    connect(table, &QTableWidget::itemSelectionChanged, this, &SurvivorsDialog::selectionChanged);
    connect(pins_table, &QTableWidget::itemSelectionChanged, this, &SurvivorsDialog::selectionChanged);
    refresh();
}

void SurvivorsDialog::refresh()
{
    shown = source.survivors();
    shown_pins = source.pins();

    table->setRowCount(0);
    for (auto &s : shown)
    {
        int row = table->rowCount();
        table->insertRow(row);
        QString instruction = s.before.empty() ? (s.flags & codeflag::Dma ? tr("graphics") : tr("data"))
                                               : QString::fromStdString(s.before) + " → " + QString::fromStdString(s.after);
        QStringList cells = { where(s.offset, s.address), hex2(s.was) + " → " + hex2(s.now), instruction,
                              tr("%1 frames").arg(QLocale().toString(qulonglong(s.frames_used))) };
        for (int c = 0; c < cells.size(); c++)
            table->setItem(row, c, new QTableWidgetItem(cells[c]));
    }
    table->resizeColumnsToContents();
    table->setVisible(!shown.empty());
    empty_label->setVisible(shown.empty());

    pins_table->setRowCount(0);
    for (auto &p : shown_pins)
    {
        int row = pins_table->rowCount();
        pins_table->insertRow(row);
        QStringList cells = { where(p.offset, std::nullopt), hex2(p.was) + " → " + hex2(p.value),
                              QString::fromStdString(p.note) };
        for (int c = 0; c < cells.size(); c++)
            pins_table->setItem(row, c, new QTableWidgetItem(cells[c]));
    }
    pins_table->resizeColumnsToContents();
    selectionChanged();
}

void SurvivorsDialog::selectionChanged()
{
    pin_button->setEnabled(!selected_rows(table).empty());
    unpin_button->setEnabled(!selected_rows(pins_table).empty());
}

void SurvivorsDialog::pinSelected()
{
    std::vector<uint32_t> offsets;
    for (int row : selected_rows(table))
        if (size_t(row) < shown.size())
            offsets.push_back(shown[size_t(row)].offset);
    if (offsets.empty())
        return;
    auto err = source.pin(offsets, note_edit->text().trimmed().toStdString());
    message->setText(!err.empty()            ? QString::fromStdString(err)
                     : offsets.size() == 1 ? tr("Pinned 1 byte. Save the recipe to keep it.")
                                           : tr("Pinned %1 bytes. Save the recipe to keep them.").arg(offsets.size()));
    if (err.empty())
        note_edit->clear();
    refresh();
}

void SurvivorsDialog::unpinSelected()
{
    std::vector<uint32_t> offsets;
    for (int row : selected_rows(pins_table))
        if (size_t(row) < shown_pins.size())
            offsets.push_back(shown_pins[size_t(row)].offset);
    if (offsets.empty())
        return;
    source.unpin(offsets);
    message->setText(tr("Unpinned. The bytes stay as they are until the next run starts."));
    refresh();
}
