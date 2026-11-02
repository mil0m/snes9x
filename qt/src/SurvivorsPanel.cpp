#include "SurvivorsPanel.hpp"
#include "UiKit.hpp"

#include <algorithm>

#include <QFontDatabase>
#include <QItemSelectionModel>

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

// Offsets into the ROM, like the memory viewer shows them; the CPU's address in the tooltip.
QString where(uint32_t offset)
{
    return QString::fromStdString(hex_address(offset, Target::ROM));
}

QString cpu(std::optional<uint32_t> address)
{
    if (!address)
        return {};
    auto bank = QString("%1").arg(*address >> 16, 2, 16, QChar('0')).toUpper();
    auto at = QString("%1").arg(*address & 0xFFFF, 4, 16, QChar('0')).toUpper();
    return QObject::tr("CPU address $%1:%2").arg(bank, at);
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
    t->setAccessibleName(name);
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

SurvivorsPanel::SurvivorsPanel(SurvivorSource &source, QWidget *parent) : QWidget(parent), source(source)
{
    setWindowTitle(tr("Survivors and Pins"));
    auto layout = new QVBoxLayout(this);

    layout->addWidget(ui::heading(tr("Survivors"), this));
    auto about = new QLabel(tr("<b>Survivors</b> are ROM changes the game has used in 100 frames or more, with no "
                               "crash in the 15 seconds after. <b>Pin</b> the ones you like to keep them in the "
                               "recipe: pins apply whenever a run starts, before the rules, and the crash guard "
                               "never undoes them. Save the recipe to keep its pins."),
                            this);
    about->setWordWrap(true);
    ui::setTone(about, ui::Tone::Secondary);
    layout->addWidget(about);

    empty_label = new QLabel(tr("No survivors yet. Turn on a recipe that changes the ROM and play: changes the "
                                "game keeps using without crashing show up here."),
                             this);
    empty_label->setObjectName("empty");
    empty_label->setWordWrap(true);
    layout->addWidget(empty_label);

    table = make_table(this, "survivors", { tr("Where"), tr("Change"), tr("Instruction"), tr("Used") });
    // The instruction takes the room; the rest fit their contents.
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(table, 2);

    pin_row = new QWidget(this);
    auto pin_layout = new QHBoxLayout(pin_row);
    pin_layout->setContentsMargins(0, 0, 0, 0);
    note_edit = new QLineEdit(this);
    note_edit->setObjectName("note");
    note_edit->setPlaceholderText(tr("A note for the pin, like \"doors stay shut\" (optional)"));
    note_edit->setAccessibleName(tr("Note for the pin"));
    pin_button = new QPushButton(ui::icon("pin"), tr("Pin Selected"), this);
    pin_button->setObjectName("pin");
    pin_button->setAutoDefault(false);
    pin_layout->addWidget(note_edit, 1);
    pin_layout->addWidget(pin_button);
    layout->addWidget(pin_row);

    message = new ui::StatusLine(this);
    message->setObjectName("message");
    layout->addWidget(message);

    layout->addSpacing(ui::sectionGap(this) / 2);
    layout->addWidget(ui::heading(tr("Pinned in this recipe"), this));
    pins_empty = new QLabel(tr("Nothing pinned yet. Select survivors above, then Pin Selected."), this);
    pins_empty->setObjectName("pins_empty");
    pins_empty->setWordWrap(true);
    ui::setTone(pins_empty, ui::Tone::Secondary);
    layout->addWidget(pins_empty);
    pins_table = make_table(this, "pins", { tr("Where"), tr("Change"), tr("Note") });
    pins_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    pins_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(pins_table, 1);
    layout->addStretch(0); // takes the slack only when both tables are hidden

    auto bottom = new QHBoxLayout;
    unpin_button = new QPushButton(ui::icon("pinned-off"), tr("Unpin Selected"), this);
    unpin_button->setObjectName("unpin");
    unpin_button->setAutoDefault(false);
    bottom->addWidget(unpin_button);
    bottom->addStretch(1);
    layout->addLayout(bottom);

    connect(pin_button, &QPushButton::clicked, this, &SurvivorsPanel::pinSelected);
    connect(unpin_button, &QPushButton::clicked, this, &SurvivorsPanel::unpinSelected);
    connect(table, &QTableWidget::itemSelectionChanged, this, &SurvivorsPanel::selectionChanged);
    connect(pins_table, &QTableWidget::itemSelectionChanged, this, &SurvivorsPanel::selectionChanged);
    refresh();
}

void SurvivorsPanel::refresh()
{
    auto survivors = source.survivors();
    auto pins = source.pins();
    auto same = [](auto &a, auto &b) {
        return a.size() == b.size() &&
               std::equal(a.begin(), a.end(), b.begin(), [](auto &x, auto &y) { return x.offset == y.offset; });
    };
    if (filled && same(survivors, shown) && same(pins, shown_pins))
    {
        // The same bytes: only how often they were used moves. Selection and scroll stay put.
        shown = std::move(survivors);
        for (size_t i = 0; i < shown.size(); i++)
            if (auto item = table->item(int(i), 3))
                item->setText(tr("%1 frames").arg(QLocale().toString(qulonglong(shown[i].frames_used))));
        return;
    }
    std::vector<uint32_t> picked, picked_pins; // still selected after the lists change
    for (int row : selected_rows(table))
        if (size_t(row) < shown.size())
            picked.push_back(shown[size_t(row)].offset);
    for (int row : selected_rows(pins_table))
        if (size_t(row) < shown_pins.size())
            picked_pins.push_back(shown_pins[size_t(row)].offset);
    shown = std::move(survivors);
    shown_pins = std::move(pins);

    table->setRowCount(0);
    for (auto &s : shown)
    {
        int row = table->rowCount();
        table->insertRow(row);
        QString instruction = s.before.empty() ? (s.flags & codeflag::Dma ? tr("graphics") : tr("data"))
                                               : QString::fromStdString(s.before) + " → " + QString::fromStdString(s.after);
        QStringList cells = { where(s.offset), hex2(s.was) + " → " + hex2(s.now), instruction,
                              tr("%1 frames").arg(QLocale().toString(qulonglong(s.frames_used))) };
        for (int c = 0; c < cells.size(); c++)
        {
            auto item = new QTableWidgetItem(cells[c]);
            item->setToolTip(cpu(s.address));
            if (c < 2) // addresses and bytes in the viewer's fixed-width font
                item->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            table->setItem(row, c, item);
        }
    }
    table->setVisible(!shown.empty());
    pin_row->setVisible(!shown.empty());
    empty_label->setVisible(shown.empty());

    pins_table->setRowCount(0);
    for (auto &p : shown_pins)
    {
        int row = pins_table->rowCount();
        pins_table->insertRow(row);
        QStringList cells = { where(p.offset), hex2(p.was) + " → " + hex2(p.value),
                              QString::fromStdString(p.note) };
        for (int c = 0; c < cells.size(); c++)
        {
            auto item = new QTableWidgetItem(cells[c]);
            if (c < 2)
                item->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            pins_table->setItem(row, c, item);
        }
    }
    pins_table->setVisible(!shown_pins.empty());
    unpin_button->setVisible(!shown_pins.empty());
    pins_empty->setVisible(shown_pins.empty());
    auto reselect = [](QTableWidget *t, auto &rows, const std::vector<uint32_t> &offsets) {
        for (size_t i = 0; i < rows.size(); i++)
            if (std::find(offsets.begin(), offsets.end(), rows[i].offset) != offsets.end())
                t->selectionModel()->select(t->model()->index(int(i), 0),
                                            QItemSelectionModel::Select | QItemSelectionModel::Rows);
    };
    reselect(table, shown, picked);
    reselect(pins_table, shown_pins, picked_pins);
    filled = true;
    selectionChanged();
}

void SurvivorsPanel::selectionChanged()
{
    pin_button->setEnabled(!selected_rows(table).empty());
    unpin_button->setEnabled(!selected_rows(pins_table).empty());
}

void SurvivorsPanel::pinSelected()
{
    std::vector<uint32_t> offsets;
    for (int row : selected_rows(table))
        if (size_t(row) < shown.size())
            offsets.push_back(shown[size_t(row)].offset);
    if (offsets.empty())
        return;
    auto err = source.pin(offsets, note_edit->text().trimmed().toStdString());
    message->setMessage(!err.empty()            ? tr("Couldn't pin: %1").arg(QString::fromStdString(err))
                        : offsets.size() == 1 ? tr("Pinned 1 byte. Save the recipe to keep it.")
                                              : tr("Pinned %1 bytes. Save the recipe to keep them.").arg(offsets.size()),
                        err.empty() ? ui::Tone::Ok : ui::Tone::Error);
    if (err.empty())
        note_edit->clear();
    refresh();
}

void SurvivorsPanel::unpinSelected()
{
    std::vector<uint32_t> offsets;
    for (int row : selected_rows(pins_table))
        if (size_t(row) < shown_pins.size())
            offsets.push_back(shown_pins[size_t(row)].offset);
    if (offsets.empty())
        return;
    source.unpin(offsets);
    message->setMessage(tr("Unpinned. The ROM keeps that change until the run restarts or you go back to normal; "
                        "then it's gone."));
    refresh();
}
