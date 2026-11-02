#include "WorkshopPages.hpp"
#include "IntensitySlider.hpp"
#include "RecipeActions.hpp"
#include "SurvivorsPanel.hpp"
#include "UiKit.hpp"
#include "corrupt/rule_text.h"

#include <QCheckBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <chrono>

using namespace corrupt;

namespace
{

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

QString plural(qulonglong n, const QString &one, const QString &many)
{
    return n == 1 ? one : many.arg(QLocale().toString(n));
}

QPushButton *button(const QString &text, QWidget *parent, const char *icon = nullptr)
{
    auto b = new QPushButton(text, parent);
    b->setAutoDefault(false);
    if (icon)
        b->setIcon(ui::icon(icon));
    return b;
}

QPushButton *tool(const char *icon, const QString &what, QWidget *parent)
{
    auto b = new QPushButton(parent);
    b->setAutoDefault(false);
    b->setIcon(ui::icon(icon));
    b->setToolTip(what);
    b->setAccessibleName(what);
    return b;
}

QLabel *help(const QString &text, QWidget *parent)
{
    auto label = new QLabel(text, parent);
    label->setWordWrap(true);
    ui::setTone(label, ui::Tone::Secondary);
    return label;
}

QVBoxLayout *pageLayout(QWidget *page)
{
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    return layout;
}

void fitTable(QTableWidget *table)
{
    // Exactly as tall as its rows: no blank strip that looks like another row.
    table->resizeRowsToContents();
    int height = table->horizontalHeader()->sizeHint().height() + 2 * table->frameWidth();
    for (int r = 0; r < table->rowCount(); r++)
        height += table->rowHeight(r);
    table->setFixedHeight(height);
}

QString policyText(PolicyKind p)
{
    switch (p)
    {
    case PolicyKind::Reroll: return QObject::tr("Re-roll the guilty rule's dice");
    case PolicyKind::Skip: return QObject::tr("Skip the guilty rule's last changes");
    case PolicyKind::Dampen: return QObject::tr("Halve the guilty rule's strength");
    case PolicyKind::Disable: return QObject::tr("Turn the guilty rule off");
    case PolicyKind::Escalate: return QObject::tr("Re-roll twice, then halve, then turn off");
    }
    return {};
}

QString ago(std::filesystem::file_time_type when)
{
    if (when == std::filesystem::file_time_type{})
        return {};
    auto now = std::filesystem::file_time_type::clock::now();
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(now - when).count();
    if (minutes < 1)
        return QObject::tr("just now");
    if (minutes < 60)
        return minutes == 1 ? QObject::tr("a minute ago") : QObject::tr("%1 minutes ago").arg(minutes);
    auto hours = minutes / 60;
    if (hours < 24)
        return hours == 1 ? QObject::tr("an hour ago") : QObject::tr("%1 hours ago").arg(hours);
    auto days = hours / 24;
    return days == 1 ? QObject::tr("yesterday") : QObject::tr("%1 days ago").arg(days);
}

} // namespace

// ---------------------------------------------------------------- Recipe

RecipePage::RecipePage(CorruptionControl &control, RecipeActions &actions, QWidget *parent)
    : QWidget(parent), control(control), actions(actions)
{
    setObjectName("recipe_page");
    auto layout = pageLayout(this);

    name_label = new QLabel(this);
    name_label->setObjectName("recipe_name");
    QFont title = name_label->font();
    title.setBold(true);
    title.setPointSizeF(title.pointSizeF() * 1.15);
    name_label->setFont(title);
    name_label->setWordWrap(true);
    layout->addWidget(name_label);
    notes_label = help({}, this);
    notes_label->setObjectName("recipe_notes");
    layout->addWidget(notes_label);

    auto file_row = new QHBoxLayout;
    auto new_button = button(tr("New Recipe…"), this, "file-plus");
    new_button->setObjectName("new_recipe");
    new_button->setToolTip(tr("Start an empty recipe for this game"));
    save_button = button(tr("Save"), this, "device-floppy");
    save_button->setObjectName("save");
    auto save_as = button(tr("Save As…"), this);
    save_as->setObjectName("save_as");
    auto share = button(tr("Share…"), this);
    share->setObjectName("share");
    share->setToolTip(tr("One file with the recipe, its pins and its start moment, to send to a friend"));
    auto import = button(tr("Import…"), this);
    import->setObjectName("import");
    import->setToolTip(tr("Add a recipe a friend shared (.s9xrecipe) to this game's recipes"));
    for (auto b : { new_button, save_button, save_as, share, import })
        file_row->addWidget(b);
    file_row->addStretch(1);
    layout->addLayout(file_row);
    moments_check = new QCheckBox(tr("Remember where each run starts"), this);
    moments_check->setObjectName("moments");
    moments_check->setToolTip(tr("Save the moment a run started (a save state) with the recipe, and go back to it "
                                 "when the recipe is loaded, so the same seed plays out the same way."));
    layout->addLayout(ui::row(moments_check));
    message = new ui::StatusLine(this);
    message->setObjectName("recipe_message");
    message->hide();
    layout->addWidget(message);
    connect(new_button, &QPushButton::clicked, this, [this] { this->actions.newRecipe(); });
    connect(save_button, &QPushButton::clicked, this, [this] { this->actions.save(); });
    connect(save_as, &QPushButton::clicked, this, [this] { this->actions.saveAs(); });
    connect(share, &QPushButton::clicked, this, [this] { this->actions.share(); });
    connect(import, &QPushButton::clicked, this, [this] { this->actions.import(); });
    connect(moments_check, &QCheckBox::toggled, this, [this](bool on) { this->control.setSavedMoments(on); });

    // Rules, in full.
    layout->addSpacing(ui::sectionGap(this));
    layout->addWidget(ui::heading(tr("Rules"), this));
    rules_table = new QTableWidget(0, kColumns, this);
    rules_table->setObjectName("rules");
    rules_table->setHorizontalHeaderLabels({ tr("On"), tr("Rule"), tr("What it does"), tr("Strength"), tr("Crashes") });
    rules_table->horizontalHeaderItem(kStrength)->setToolTip(tr("How hard this rule hits, on top of Chaos"));
    rules_table->horizontalHeaderItem(kCrashes)->setToolTip(tr("How often the crash guard blamed this rule"));
    rules_table->horizontalHeaderItem(kCrashes)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rules_table->verticalHeader()->setVisible(false);
    auto header = rules_table->horizontalHeader();
    header->setSectionResizeMode(kOn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kName, QHeaderView::Interactive);
    header->setSectionResizeMode(kWhat, QHeaderView::Stretch);
    header->setSectionResizeMode(kStrength, QHeaderView::Fixed);
    header->setSectionResizeMode(kCrashes, QHeaderView::ResizeToContents);
    header->setHighlightSections(false);
    rules_table->setColumnWidth(kStrength, fontMetrics().horizontalAdvance("100%") + 72);
    rules_table->setWordWrap(true);
    rules_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rules_table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rules_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rules_table->setSelectionMode(QAbstractItemView::SingleSelection);
    rules_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    rules_table->setToolTip(tr("Each rule changes some memory now and then. Double-click one to edit it."));
    rules_table->setAccessibleName(tr("Rules"));
    layout->addWidget(rules_table);
    connect(header, &QHeaderView::sectionResized, this, [this](int column) {
        if (column == kWhat)
            QTimer::singleShot(0, this, [this] { fitTables(); });
    });
    rules_empty = help(tr("No rules yet. Add Rule makes one; so does selecting bytes in Memory."), this);
    rules_empty->setObjectName("rules_empty");
    layout->addWidget(rules_empty);
    auto rule_row = new QHBoxLayout;
    add_button = button(tr("Add Rule…"), this, "plus");
    add_button->setObjectName("add_rule");
    add_button->setToolTip(tr("A new rule: which memory, what to do to it, and when"));
    edit_button = tool("pencil", tr("Edit the selected rule"), this);
    edit_button->setObjectName("edit_rule");
    duplicate_button = tool("copy", tr("Duplicate the selected rule"), this);
    duplicate_button->setObjectName("duplicate_rule");
    remove_button = tool("trash", tr("Remove the selected rule"), this);
    remove_button->setObjectName("remove_rule");
    for (auto b : { add_button, edit_button, duplicate_button, remove_button })
        rule_row->addWidget(b);
    rule_row->addStretch(1);
    layout->addLayout(rule_row);
    connect(add_button, &QPushButton::clicked, this, [this] { this->actions.addRule(); });
    connect(edit_button, &QPushButton::clicked, this, [this] { this->actions.editRule(selectedRule()); });
    connect(duplicate_button, &QPushButton::clicked, this, [this] { this->actions.duplicateRule(selectedRule()); });
    connect(remove_button, &QPushButton::clicked, this, [this] { this->actions.removeRule(selectedRule()); });
    connect(rules_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column != kOn && column != kStrength)
            this->actions.editRule(row);
    });
    connect(rules_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item->column() == kOn)
            this->control.setRuleEnabled(size_t(item->row()), item->checkState() == Qt::Checked);
    });
    connect(rules_table, &QTableWidget::itemSelectionChanged, this, [this] {
        bool any = selectedRule() >= 0;
        edit_button->setEnabled(any);
        duplicate_button->setEnabled(any);
        remove_button->setEnabled(any);
    });

    // Everything else the recipe keeps.
    layout->addSpacing(ui::sectionGap(this));
    layout->addWidget(ui::heading(tr("Also in This Recipe"), this));
    kept_table = new QTableWidget(0, 2, this);
    kept_table->setObjectName("kept");
    kept_table->setHorizontalHeaderLabels({ tr("What"), tr("Details") });
    kept_table->verticalHeader()->setVisible(false);
    kept_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    kept_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    kept_table->horizontalHeader()->setHighlightSections(false);
    kept_table->setWordWrap(true);
    kept_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    kept_table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    kept_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    kept_table->setSelectionMode(QAbstractItemView::SingleSelection);
    kept_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    kept_table->setAccessibleName(tr("Also in this recipe"));
    layout->addWidget(kept_table);
    kept_empty = help(tr("Nothing else yet. Switches, changes and protected functions come from Functions, pins "
                         "from the ROM page, painted bytes from Memory."),
                      this);
    kept_empty->setObjectName("kept_empty");
    layout->addWidget(kept_empty);
    auto kept_row = new QHBoxLayout;
    remove_kept_button = button(tr("Remove from Recipe"), this, "trash");
    remove_kept_button->setObjectName("remove_kept");
    remove_kept_button->setToolTip(tr("Take the selected line out of the recipe. What it changed in the game stays "
                                      "until the next run starts."));
    kept_row->addWidget(remove_kept_button);
    kept_row->addStretch(1);
    layout->addLayout(kept_row);
    connect(kept_table, &QTableWidget::itemSelectionChanged, this,
            [this] { remove_kept_button->setEnabled(!kept_table->selectionModel()->selectedRows().isEmpty()); });
    connect(remove_kept_button, &QPushButton::clicked, this, [this] {
        auto rows = kept_table->selectionModel()->selectedRows();
        if (rows.isEmpty() || size_t(rows.first().row()) >= kept.size())
            return;
        auto err = this->control.removeKept(kept[size_t(rows.first().row())]);
        if (!err.empty())
            say(qs(err), true);
        refresh(this->control.status());
    });
    layout->addStretch(1);

    edit_button->setEnabled(false);
    duplicate_button->setEnabled(false);
    remove_button->setEnabled(false);
    remove_kept_button->setEnabled(false);
}

void RecipePage::say(const QString &text, bool error)
{
    message->setMessage(text, error ? ui::Tone::Error : ui::Tone::Normal);
    message->show();
}

int RecipePage::selectedRule() const
{
    auto rows = rules_table->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}

void RecipePage::refresh(const PanelStatus &status)
{
    has_recipe = status.has_recipe;
    name_label->setText(status.has_recipe ? qs(status.engine.recipe_name)
                        : status.game_loaded ? tr("No recipe loaded")
                                             : tr("No game loaded"));
    QString notes = qs(status.notes);
    if (!status.has_recipe)
        notes = status.game_loaded ? tr("Pick one in the panel, or New starts an empty one.")
                                   : tr("Load a game, then pick a recipe in the panel.");
    notes_label->setText(notes);
    notes_label->setVisible(!notes.isEmpty());
    {
        QSignalBlocker block(moments_check);
        moments_check->setChecked(status.saved_moments);
    }
    save_button->setEnabled(status.has_recipe && (status.edited || status.new_moment || status.recipe_path.empty()));
    add_button->setEnabled(status.has_recipe);

    if (status.recipe_generation != shown_generation || status.rules_generation != shown_rules_generation)
    {
        shown_generation = status.recipe_generation;
        shown_rules_generation = status.rules_generation;
        rebuildRules(status);
    }
    if (status.rule_crashes != shown_crashes)
    {
        shown_crashes = status.rule_crashes;
        for (int i = 0; i < rules_table->rowCount() && i < int(status.rule_crashes.size()); i++)
            if (auto item = rules_table->item(i, kCrashes))
                item->setText(QString::number(status.rule_crashes[size_t(i)]));
    }
    for (size_t i = 0; i < rule_sliders.size() && i < status.rules.size(); i++)
        rule_sliders[i]->refresh(status.rules[i].intensity);
    {
        QSignalBlocker block(rules_table);
        for (int i = 0; i < rules_table->rowCount() && i < int(status.rules.size()); i++)
            if (auto on = rules_table->item(i, kOn))
                on->setCheckState(status.rules[size_t(i)].enabled ? Qt::Checked : Qt::Unchecked);
    }
    rules_empty->setVisible(status.has_recipe && status.rules.empty());

    bool same = status.kept.size() == kept.size();
    for (size_t i = 0; same && i < kept.size(); i++)
        same = kept[i].kind == status.kept[i].kind && kept[i].at == status.kept[i].at &&
               kept[i].what == status.kept[i].what && kept[i].details == status.kept[i].details;
    if (!same)
        rebuildKept(status);
    kept_table->setVisible(!kept.empty());
    kept_empty->setVisible(kept.empty() && status.has_recipe);
    remove_kept_button->setVisible(!kept.empty());
}

void RecipePage::rebuildRules(const PanelStatus &status)
{
    int selected = selectedRule();
    QSignalBlocker block(rules_table);
    rule_sliders.clear(); // the table owns and deletes them
    rules_table->setRowCount(0);
    rules_table->setRowCount(int(status.rules.size()));
    for (int i = 0; i < int(status.rules.size()); i++)
    {
        auto &r = status.rules[size_t(i)];
        auto on = new QTableWidgetItem;
        on->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        on->setCheckState(r.enabled ? Qt::Checked : Qt::Unchecked);
        rules_table->setItem(i, kOn, on);
        auto name = new QTableWidgetItem(qs(r.name));
        name->setToolTip(qs(r.name));
        rules_table->setItem(i, kName, name);
        auto what = new QTableWidgetItem(qs(describe_rule(r)));
        what->setToolTip(qs(describe_rule(r)));
        rules_table->setItem(i, kWhat, what);
        unsigned crashes = size_t(i) < status.rule_crashes.size() ? status.rule_crashes[size_t(i)] : 0;
        auto crash = new QTableWidgetItem(QString::number(crashes));
        crash->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rules_table->setItem(i, kCrashes, crash);
        auto slider = new IntensitySlider;
        slider->setIntensity(r.intensity);
        slider->setAccessibleName(tr("Strength of %1").arg(qs(r.name)));
        slider->onChanged = [this, i](double v) { control.setRuleIntensity(size_t(i), v); };
        rules_table->setCellWidget(i, kStrength, slider);
        rule_sliders.push_back(slider);
    }
    rules_table->resizeColumnToContents(kName);
    rules_table->setColumnWidth(kName, std::min(rules_table->columnWidth(kName), fontMetrics().horizontalAdvance("M") * 12));
    rules_table->setVisible(!status.rules.empty());
    if (selected >= 0 && selected < rules_table->rowCount())
        rules_table->selectRow(selected);
    fitTables();
}

void RecipePage::rebuildKept(const PanelStatus &status)
{
    kept = status.kept;
    kept_table->setRowCount(int(kept.size()));
    for (int i = 0; i < int(kept.size()); i++)
    {
        kept_table->setItem(i, 0, new QTableWidgetItem(qs(kept[size_t(i)].what)));
        kept_table->setItem(i, 1, new QTableWidgetItem(qs(kept[size_t(i)].details)));
    }
    remove_kept_button->setEnabled(!kept_table->selectionModel()->selectedRows().isEmpty());
    fitTables();
}

void RecipePage::fitTables()
{
    fitTable(rules_table);
    fitTable(kept_table);
}

// ---------------------------------------------------------------- ROM

RomPage::RomPage(CorruptionControl &control, SurvivorSource &survivors, QWidget *parent)
    : QWidget(parent), control(control)
{
    setObjectName("rom_page");
    auto layout = pageLayout(this);
    layout->addWidget(ui::heading(tr("The ROM (the Game Itself)"), this));
    changed_label = new QLabel(this);
    changed_label->setObjectName("rom_changed");
    changed_label->setWordWrap(true);
    layout->addWidget(changed_label);
    learned_label = help({}, this);
    learned_label->setObjectName("rom_learned");
    layout->addWidget(learned_label);
    auto row = new QHBoxLayout;
    auto export_rom = button(tr("Export ROM…"), this, "file-export");
    export_rom->setObjectName("export_rom");
    export_rom->setToolTip(tr("Save the ROM as it is now as a .sfc file (checksum fixed), for other emulators or "
                              "a flash cart"));
    restore_button = button(tr("Restore Clean ROM"), this, "restore");
    restore_button->setObjectName("restore_rom");
    restore_button->setToolTip(tr("Undo every change to the ROM now, and leave RAM as it is"));
    clear_paint_button = button(tr("Clear Paint"), this, "eraser");
    clear_paint_button->setObjectName("clear_paint");
    clear_paint_button->setToolTip(tr("Forget the bytes painted in Memory. They stay until the next run starts."));
    for (auto b : { export_rom, restore_button, clear_paint_button })
        row->addWidget(b);
    row->addStretch(1);
    layout->addLayout(row);
    message = new ui::StatusLine(this);
    message->setObjectName("rom_message");
    message->hide();
    layout->addWidget(message);
    askSavePath = [this](const QString &title, const QString &start) {
        return QFileDialog::getSaveFileName(this, title, start, tr("SNES ROM (*.sfc *.smc)"));
    };
    connect(restore_button, &QPushButton::clicked, this, [this] { this->control.restoreCleanRom(); });
    connect(clear_paint_button, &QPushButton::clicked, this, [this] { this->control.clearPatches(); });
    connect(export_rom, &QPushButton::clicked, this, [this] {
        auto status = this->control.status();
        if (!status.game_loaded)
            return;
        QString name = qs(status.game.title).trimmed().replace(QRegularExpression("[^A-Za-z0-9 _-]"), "_") +
                       (status.rom_changed ? tr(" (corrupted %1)").arg(status.engine.seed) : QString()) + ".sfc";
        auto path = askSavePath(tr("Export ROM"), QDir::home().filePath(name));
        if (path.isEmpty())
            return;
        auto err = this->control.exportRom(path.toStdString());
        message->setMessage(err.empty() ? tr("Exported %1").arg(ui::shortPath(path)) : qs(err),
                            err.empty() ? ui::Tone::Ok : ui::Tone::Error);
        message->show();
    });

    layout->addSpacing(ui::sectionGap(this));
    survivors_panel = new SurvivorsPanel(survivors, this);
    survivors_panel->layout()->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(survivors_panel);
    layout->addStretch(1);
}

void RomPage::refresh(const PanelStatus &status)
{
    if (++ticks % 4 == 0) // every two seconds: survivors keep themselves fresh (no Refresh button)
        survivors_panel->refresh();
    QStringList changed;
    if (!status.game_loaded)
        changed << tr("Load a game to see its ROM.");
    else
    {
        changed << (status.rom_changed == 0
                        ? tr("Clean: nothing has changed the game itself.")
                        : tr("%1, %2 of them used by the game so far.")
                              .arg(plural(status.rom_changed, tr("1 byte changed"), tr("%1 bytes changed")))
                              .arg(QLocale().toString(qulonglong(status.rom_used))));
        QStringList kept;
        if (status.functions_off)
            kept << plural(status.functions_off, tr("1 function off"), tr("%1 functions off"));
        if (status.functions_guarded)
            kept << plural(status.functions_guarded, tr("1 guarded"), tr("%1 guarded"));
        if (status.pins)
            kept << plural(status.pins, tr("1 pinned"), tr("%1 pinned"));
        if (status.patched)
            kept << plural(status.patched, tr("1 byte painted"), tr("%1 bytes painted"));
        if (status.instant_reverts)
            kept << plural(status.instant_reverts, tr("1 bad jump put back at once"), tr("%1 bad jumps put back at once"));
        if (status.tested_first)
            kept << tr("%1 tested first (%2 re-rolled, %3 skipped)")
                        .arg(plural(status.tested_first, tr("1 change"), tr("%1 changes")))
                        .arg(status.tested_rerolled)
                        .arg(status.tested_skipped);
        if (!kept.isEmpty())
            changed << kept.join(" · ");
    }
    changed_label->setText(changed.join("\n"));

    QStringList learned;
    if (status.game_loaded)
    {
        learned << (status.code.seen ? tr("Known so far: %1 bytes of code, %2 of data, %3 of graphics. It learns as "
                                          "you play, and from everyone sharing the library.")
                                           .arg(QLocale().toString(qulonglong(status.code.code)),
                                                QLocale().toString(qulonglong(status.code.data)),
                                                QLocale().toString(qulonglong(status.code.dma)))
                                     : tr("Nothing known about its code yet: it learns as you play."));
        if (status.functions)
            learned << plural(status.functions, tr("1 function known."), tr("%1 functions known."));
        if (status.jump_tables)
            learned << tr("%1, %2 with entries to swap.")
                           .arg(plural(status.jump_tables, tr("1 jump table found"), tr("%1 jump tables found")))
                           .arg(QLocale().toString(qulonglong(status.jump_swappable)));
    }
    learned_label->setText(learned.join(" "));
    learned_label->setVisible(!learned.isEmpty());
    restore_button->setEnabled(status.rom_changed > 0);
    clear_paint_button->setEnabled(status.patched > 0);
}

// ---------------------------------------------------------------- Safety

SafetyPage::SafetyPage(CorruptionControl &control, QWidget *parent) : QWidget(parent), control(control)
{
    setObjectName("safety_page");
    auto layout = pageLayout(this);
    layout->addWidget(ui::heading(tr("Crash Guard"), this));
    detect_check = new QCheckBox(tr("Catch crashes and go back a few seconds"), this);
    detect_check->setObjectName("detect");
    detect_check->setToolTip(tr("Notices when the game freezes, goes black, stops or reboots itself, takes it back "
                                "to a moment before, and changes what caused it so it plays on."));
    layout->addLayout(ui::row(detect_check));
    test_first_check = new QCheckBox(tr("Try ROM changes ahead first"), this);
    test_first_check->setObjectName("test_first");
    test_first_check->setToolTip(tr("Before a live ROM rule changes the game, plays a few seconds ahead with the "
                                    "change, unseen, and rolls again if the game would crash. Costs a short pause "
                                    "each time."));
    layout->addLayout(ui::row(test_first_check));
    instant_check = new QCheckBox(tr("Put back a ROM change that sends the game astray, at once"), this);
    instant_check->setObjectName("instant_revert");
    instant_check->setToolTip(tr("When a changed ROM byte makes the game jump into something that isn't code, puts "
                                 "it back the moment it happens and plays the frame again, before it can crash."));
    layout->addLayout(ui::row(instant_check));
    auto form = ui::form();
    policy_combo = new QComboBox(this);
    policy_combo->setObjectName("policy");
    policy_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    policy_combo->setMinimumContentsLength(16);
    policy_combo->addItem(tr("Whatever the recipe says"), QString());
    for (auto p : { PolicyKind::Reroll, PolicyKind::Skip, PolicyKind::Dampen, PolicyKind::Disable, PolicyKind::Escalate })
        policy_combo->addItem(policyText(p), QString(policy_name(p)));
    policy_combo->setToolTip(tr("What to do with the rule that caused a crash. Changed ROM bytes the game used "
                                "are put back either way."));
    form->addRow(tr("After a crash"), policy_combo);
    layout->addLayout(form);
    crash_label = new QLabel(this);
    crash_label->setObjectName("crash_status");
    crash_label->setWordWrap(true);
    layout->addWidget(crash_label);
    auto mark_row = new QHBoxLayout;
    auto mark = button(tr("It Crashed: Go Back"), this, "history");
    mark->setObjectName("mark_crash");
    mark->setToolTip(tr("For a crash the guard missed: go back about two seconds and blame what changed."));
    mark_row->addWidget(mark);
    mark_row->addStretch(1);
    layout->addLayout(mark_row);
    connect(detect_check, &QCheckBox::clicked, this, [this](bool on) { this->control.setDetectionEnabled(on); });
    connect(test_first_check, &QCheckBox::clicked, this, [this](bool on) { this->control.setTestFirstEnabled(on); });
    connect(instant_check, &QCheckBox::clicked, this, [this](bool on) { this->control.setInstantEnabled(on); });
    connect(policy_combo, &QComboBox::activated, this, [this](int index) {
        auto name = policy_combo->itemData(index).toString().toStdString();
        this->control.setPolicyOverride(name.empty() ? std::nullopt : parse_policy(name));
    });
    connect(mark, &QPushButton::clicked, this, [this] { this->control.markCrash(); });

    layout->addSpacing(ui::sectionGap(this));
    layout->addWidget(ui::heading(tr("Game Save Backups"), this));
    layout->addWidget(help(tr("Your in-game saves (the cartridge's battery save), copied automatically while you "
                              "play. This doesn't undo corruption: Back to Normal does."),
                           this));
    backup_list = new QListWidget(this);
    backup_list->setObjectName("backups");
    backup_list->setAccessibleName(tr("Game save backups"));
    backup_list->setMaximumHeight(backup_list->fontMetrics().height() * 10);
    layout->addWidget(backup_list);
    backups_empty = help(tr("None yet: the first copy is made when the game loads with a save."), this);
    backups_empty->setObjectName("backups_empty");
    layout->addWidget(backups_empty);
    auto backup_row = new QHBoxLayout;
    auto restore = restore_backup_button = button(tr("Restore This Save…"), this, "device-floppy");
    restore->setObjectName("restore_backup");
    restore->setToolTip(tr("Replace the game's save with the selected copy and restart the game. Your current "
                           "save is copied first."));
    auto open = button(tr("Open Folder"), this, "folder-open");
    open->setObjectName("open_backups");
    backup_row->addWidget(restore);
    backup_row->addWidget(open);
    backup_row->addStretch(1);
    layout->addLayout(backup_row);
    message = new ui::StatusLine(this);
    message->setObjectName("safety_message");
    message->hide();
    layout->addWidget(message);
    confirm = [this](const QString &title, const QString &question) {
        return QMessageBox::question(this, title, question) == QMessageBox::Yes;
    };
    connect(open, &QPushButton::clicked, this, [this] {
        auto dir = qs(this->control.backupDir().string());
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(restore, &QPushButton::clicked, this, [this] {
        auto item = backup_list->currentItem();
        if (!item)
        {
            message->setMessage(tr("Pick a backup first"), ui::Tone::Error);
            return message->show();
        }
        if (!confirm(tr("Restore this save"),
                     tr("Replace the game's save (your in-game progress) with the copy from %1, and restart the game? "
                        "Your current save is copied first, so you can come back to it.")
                         .arg(item->text())))
            return;
        auto err = this->control.restoreBackup(item->data(Qt::UserRole).toString().toStdString());
        message->setMessage(err.empty() ? tr("Save restored and game reset. Your previous save was backed up first.")
                                        : tr("Couldn't restore: %1").arg(qs(err)),
                            err.empty() ? ui::Tone::Ok : ui::Tone::Error);
        message->show();
        refreshBackups();
    });
    layout->addStretch(1);
}

void SafetyPage::refresh(const PanelStatus &status)
{
    QSignalBlocker b1(detect_check), b2(test_first_check), b3(instant_check);
    detect_check->setChecked(status.detection_enabled);
    test_first_check->setChecked(status.test_first_enabled);
    instant_check->setChecked(status.instant_enabled);
    policy_combo->setItemText(0, tr("As the recipe says: %1")
                                     .arg(status.policy_overridden ? tr("its own choice") : policyText(status.policy)));
    QString text = status.crashes == 0 ? tr("No crashes yet.")
                                       : plural(status.crashes, tr("1 crash caught."), tr("%1 crashes caught.")) +
                                             tr(" Last: %1").arg(qs(status.last_crash));
    if (status.hot_blocks)
        text += "\n" + plural(status.hot_blocks, tr("It has learned 1 dangerous spot (rules can avoid it)."),
                              tr("It has learned %1 dangerous spots (rules can avoid them)."));
    crash_label->setText(text);
    if (ticks++ % 8 == 0) // new automatic backups show up every few seconds
        refreshBackups();
}

void SafetyPage::refreshBackups()
{
    auto current = backup_list->currentItem() ? backup_list->currentItem()->data(Qt::UserRole).toString() : QString();
    backup_list->clear();
    for (auto &b : control.backups())
    {
        auto when = QDateTime::fromSecsSinceEpoch(
            std::chrono::duration_cast<std::chrono::seconds>(b.time.time_since_epoch()).count());
        auto name = qs(b.path.filename().string());
        auto reason = name.section('_', 2).section('.', 0, 0);
        QString why = reason == "load"             ? tr("when the game loaded")
                      : reason == "close"          ? tr("when the game closed")
                      : reason == "auto"           ? tr("while playing")
                      : reason == "before-restore" ? tr("before a restore")
                                                   : reason;
        auto item = new QListWidgetItem(QLocale().toString(when, QLocale::ShortFormat) + "  ·  " + why, backup_list);
        item->setData(Qt::UserRole, qs(b.path.string()));
        if (item->data(Qt::UserRole).toString() == current)
            backup_list->setCurrentItem(item);
    }
    backup_list->setVisible(backup_list->count() > 0);
    backups_empty->setVisible(backup_list->count() == 0);
    restore_backup_button->setVisible(backup_list->count() > 0);
}

// ---------------------------------------------------------------- Sidebar

std::array<SidebarNote, 6> sidebar_notes(const PanelStatus &status, size_t others, bool remote_on)
{
    std::array<SidebarNote, 6> notes;
    auto note = [&](WorkshopPage page) -> SidebarNote & { return notes[size_t(page)]; };
    auto count = [](size_t n, const char *one, const char *many) {
        return n == 1 ? QObject::tr(one) : QObject::tr(many).arg(QLocale().toString(qulonglong(n)));
    };
    if (status.has_recipe)
    {
        QString text = count(status.rules.size(), "1 rule", "%1 rules");
        if (status.edited && !status.recipe_path.empty())
            text += QObject::tr(" · unsaved");
        note(WorkshopPage::Recipe).text = text;
    }
    if (status.game_loaded)
    {
        if (status.functions_off)
            note(WorkshopPage::Functions).text = count(status.functions_off, "1 off", "%1 off");
        if (status.patched)
            note(WorkshopPage::Memory).text = count(status.patched, "1 painted", "%1 painted");
        if (status.rom_changed)
            note(WorkshopPage::Rom).text = count(status.rom_changed, "1 changed", "%1 changed");
        if (!status.detection_enabled)
            note(WorkshopPage::Safety) = { QObject::tr("guard off"), true };
        else if (status.crashes)
            note(WorkshopPage::Safety) = { count(status.crashes, "1 crash", "%1 crashes"), true };
    }
    if (remote_on)
        note(WorkshopPage::Sharing).text = QObject::tr("remote on");
    else if (others)
        note(WorkshopPage::Sharing).text = count(others, "1 here", "%1 here");
    return notes;
}

// ---------------------------------------------------------------- Sharing

SharingPage::SharingPage(CorruptionControl &control, QWidget *parent) : QWidget(parent), control(control)
{
    setObjectName("sharing_page");
    auto layout = pageLayout(this);
    layout->addWidget(ui::heading(tr("Library"), this));
    layout->addWidget(help(tr("The folder where recipes, named areas and what the app learns about each game are "
                              "kept. Point it at a folder you share (a git clone, or one Syncthing keeps in step) "
                              "and you play with each other's recipes and maps."),
                           this));
    library_label = new ui::ElidedLabel(this);
    library_label->setObjectName("library");
    layout->addWidget(library_label);
    auto row = new QHBoxLayout;
    auto choose = button(tr("Choose…"), this, "folder-open");
    choose->setObjectName("choose_library");
    choose->setToolTip(tr("Use another folder as the library"));
    auto open = button(tr("Open This Game's Folder"), this);
    open->setObjectName("open_game_folder");
    auto rescan = button(tr("Look for New Recipes"), this, "refresh");
    rescan->setObjectName("rescan");
    rescan->setToolTip(tr("Read the recipe files again (they're also noticed by themselves within half a minute)"));
    for (auto b : { choose, open, rescan })
        row->addWidget(b);
    row->addStretch(1);
    layout->addLayout(row);
    askFolder = [this](const QString &start) {
        return QFileDialog::getExistingDirectory(this, tr("Corruption library folder"), start);
    };
    connect(choose, &QPushButton::clicked, this, [this] {
        auto dir = askFolder(qs(this->control.libraryRoot().string()));
        if (dir.isEmpty())
            return;
        this->control.setLibraryRoot(dir.toStdString());
        if (libraryChanged)
            libraryChanged();
        refreshPlayers();
    });
    connect(open, &QPushButton::clicked, this, [this] {
        auto dir = this->control.gameFolder();
        if (dir.empty())
            return;
        QDir().mkpath(qs(dir.string()));
        QDesktopServices::openUrl(QUrl::fromLocalFile(qs(dir.string())));
    });
    connect(rescan, &QPushButton::clicked, this, [this] {
        if (lookForRecipes)
            lookForRecipes();
        refreshPlayers();
    });

    layout->addWidget(ui::heading(tr("Who's Here"), this));
    players_list = new QListWidget(this);
    players_list->setObjectName("players");
    players_list->setAccessibleName(tr("Players with files for this game"));
    players_list->setSelectionMode(QAbstractItemView::NoSelection);
    players_list->setFrameShape(QFrame::NoFrame);
    players_list->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(players_list);
    conflicts_line = new ui::StatusLine(this);
    conflicts_line->setObjectName("conflicts");
    layout->addWidget(conflicts_line);

    layout->addSpacing(ui::sectionGap(this));
    layout->addWidget(ui::heading(tr("Remote Control"), this));
    remote_check = new QCheckBox(tr("Allow remote control from a browser"), this);
    remote_check->setObjectName("remote");
    remote_check->setToolTip(tr("Lets a browser with this page's link run the corruption: on and off, Chaos, rules, "
                                "buttons and painting memory. It listens on this computer only; to reach it from "
                                "elsewhere, put it behind a tunnel with a login (see the guide)."));
    layout->addLayout(ui::row(remote_check));
    remote_label = help({}, this);
    remote_label->setObjectName("remote_status");
    layout->addWidget(remote_label);
    // The address and the link only matter while it's on.
    remote_details = new QWidget(this);
    remote_details->setObjectName("remote_details");
    auto details_layout = new QVBoxLayout(remote_details);
    details_layout->setContentsMargins(0, 0, 0, 0);
    auto form = ui::form();
    remote_address = new QLineEdit(this);
    remote_address->setObjectName("remote_address");
    remote_address->setPlaceholderText(tr("https://… (where others reach it)"));
    remote_address->setToolTip(tr("The address your tunnel gives this computer, for the link. Kept in your settings "
                                  "only. Empty: a link for this computer."));
    form->addRow(tr("Public address"), remote_address);
    details_layout->addLayout(form);
    auto remote_row = new QHBoxLayout;
    auto copy_link = button(tr("Copy Link"), this, "copy");
    copy_link->setObjectName("remote_copy");
    copy_link->setToolTip(tr("Copies the link with its key. Anyone with the link (and past your tunnel's login) can "
                             "control the corruption."));
    auto new_key = button(tr("New Key"), this, "refresh");
    new_key->setObjectName("remote_new_key");
    new_key->setToolTip(tr("Makes a new key: old links stop working and everyone connected is dropped."));
    remote_row->addWidget(copy_link);
    remote_row->addWidget(new_key);
    remote_row->addStretch(1);
    details_layout->addLayout(remote_row);
    layout->addWidget(remote_details);
    remote_details->hide();
    connect(remote_check, &QCheckBox::clicked, this, [this](bool on) {
        auto err = this->control.setRemote(on);
        if (!err.empty())
            remote_label->setText(qs(err));
    });
    connect(remote_address, &QLineEdit::editingFinished, this,
            [this] { this->control.setRemoteAddress(remote_address->text().trimmed().toStdString()); });
    connect(copy_link, &QPushButton::clicked, this, [this] {
        if (auto clip = QGuiApplication::clipboard())
            clip->setText(qs(this->control.remoteLink()));
        remote_label->setText(tr("Link copied. Send it only to people you trust with this game."));
    });
    connect(new_key, &QPushButton::clicked, this, [this] { this->control.newRemoteKey(); });
    layout->addStretch(1);

    auto remote = control.remote();
    remote_address->setText(qs(remote.address));
}

void SharingPage::refresh(const PanelStatus &status)
{
    (void)status;
    library_label->setFullText(ui::shortPath(qs(control.libraryRoot().string())));
    auto remote = control.remote();
    {
        QSignalBlocker block(remote_check);
        remote_check->setChecked(remote.enabled);
    }
    remote_details->setVisible(remote.enabled);
    if (!remote_label->text().startsWith(tr("Link copied")) || ticks % 20 == 0)
        remote_label->setText(qs(remote.status));
    if (ticks++ % 10 == 0) // the folder is read every few seconds, not on every tick
        refreshPlayers();
}

void SharingPage::refreshPlayers()
{
    players_list->clear();
    auto me = control.me();
    auto players = control.players();
    for (auto &p : players)
    {
        QString who = qs(p.who) + (p.who == me ? tr(" (you)") : QString());
        QString when = ago(p.newest);
        players_list->addItem(when.isEmpty() ? who : tr("%1 · last change %2").arg(who, when));
    }
    if (players.empty())
        players_list->addItem(control.gameFolder().empty() ? tr("Load a game to see who has played it here.")
                                                           : tr("Nobody yet: the app writes your files as you play."));
    players_list->setFixedHeight(players_list->sizeHintForRow(0) * players_list->count() + 4);
    auto conflicts = control.conflicts();
    if (conflicts.empty())
        conflicts_line->hide();
    else
    {
        conflicts_line->setMessage(plural(conflicts.size(),
                                          tr("1 file changed in two places at once and was kept twice (a "
                                             "\"sync-conflict\" copy). Open the game's folder to look."),
                                          tr("%1 files changed in two places at once and were kept twice "
                                             "(\"sync-conflict\" copies). Open the game's folder to look.")),
                                   ui::Tone::Warning);
        conflicts_line->show();
    }
}
