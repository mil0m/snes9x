#include "CorruptionDock.hpp"
#include "CorruptionSession.hpp"
#include "EmuApplication.hpp"
#include "EmuConfig.hpp"
#include "IntensitySlider.hpp"
#include "RuleEditor.hpp"
#include "corrupt/rule_text.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

using namespace corrupt;

namespace
{

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

QPushButton *button(const QString &text, QWidget *parent)
{
    auto b = new QPushButton(text, parent);
    b->setAutoDefault(false);
    return b;
}

} // namespace

CorruptionDock::CorruptionDock(EmuApplication &app, QWidget *parent)
    : QDockWidget(tr("Corruption"), parent), app(app)
{
    setObjectName("CorruptionDock");
    auto root = new QWidget(this);
    auto layout = new QVBoxLayout(root);

    game_label = new QLabel(tr("No game loaded"), root);
    game_label->setWordWrap(true);
    layout->addWidget(game_label);

    // Library folder
    auto library_row = new QHBoxLayout;
    library_edit = new QLineEdit(root);
    library_edit->setReadOnly(true);
    library_edit->setText(qs(app.config->corruption_library_root));
    auto choose = button(tr("Choose..."), root);
    library_row->addWidget(new QLabel(tr("Library"), root));
    library_row->addWidget(library_edit, 1);
    library_row->addWidget(choose);
    layout->addLayout(library_row);
    connect(choose, &QPushButton::clicked, this, &CorruptionDock::chooseLibrary);

    // Recipes
    auto recipe_box = new QGroupBox(tr("Recipe"), root);
    auto recipe_layout = new QVBoxLayout(recipe_box);
    recipe_combo = new QComboBox(recipe_box);
    recipe_layout->addWidget(recipe_combo);
    auto recipe_buttons = new QHBoxLayout;
    auto load = button(tr("Load"), recipe_box);
    auto rescan = button(tr("Rescan"), recipe_box);
    auto open_folder = button(tr("Open Folder"), recipe_box);
    recipe_buttons->addWidget(load);
    recipe_buttons->addWidget(rescan);
    recipe_buttons->addWidget(open_folder);
    recipe_layout->addLayout(recipe_buttons);
    auto file_buttons = new QHBoxLayout;
    auto new_recipe = button(tr("New"), recipe_box);
    save_button = button(tr("Save"), recipe_box);
    auto save_as = button(tr("Save As..."), recipe_box);
    file_buttons->addWidget(new_recipe);
    file_buttons->addWidget(save_button);
    file_buttons->addWidget(save_as);
    recipe_layout->addLayout(file_buttons);
    moments_check = new QCheckBox(tr("Saved moments"), recipe_box);
    moments_check->setToolTip(tr("Save the moment a run started along with the recipe, and jump back to it when "
                                 "the recipe is loaded, so the same seed replays the same run."));
    moments_check->setChecked(app.config->corruption_saved_moments);
    recipe_layout->addWidget(moments_check);
    connect(moments_check, &QCheckBox::toggled, [this](bool on) {
        this->app.config->corruption_saved_moments = on;
        CorruptionSession::get().setSavedMoments(on);
    });
    connect(new_recipe, &QPushButton::clicked, this, &CorruptionDock::newRecipe);
    connect(save_button, &QPushButton::clicked, this, &CorruptionDock::save);
    connect(save_as, &QPushButton::clicked, this, &CorruptionDock::saveAs);

    auto example_row = new QHBoxLayout;
    example_combo = new QComboBox(recipe_box);
    for (auto &file : QDir(":/corruption-examples").entryList({ "*.json" }, QDir::Files, QDir::Name))
        example_combo->addItem(QFileInfo(file).completeBaseName(), ":/corruption-examples/" + file);
    auto add_example = button(tr("Add Example"), recipe_box);
    example_row->addWidget(example_combo, 1);
    example_row->addWidget(add_example);
    recipe_layout->addLayout(example_row);
    layout->addWidget(recipe_box);

    connect(load, &QPushButton::clicked, this, &CorruptionDock::loadSelectedRecipe);
    connect(rescan, &QPushButton::clicked, this, &CorruptionDock::refreshRecipes);
    connect(add_example, &QPushButton::clicked, this, &CorruptionDock::addExample);
    connect(open_folder, &QPushButton::clicked, [this] {
        auto status = CorruptionSession::get().status();
        if (!status.game_loaded)
            return;
        auto dir = CorruptionSession::get().library().game_dir(status.game) / "recipes";
        QDir().mkpath(qs(dir.string()));
        QDesktopServices::openUrl(QUrl::fromLocalFile(qs(dir.string())));
    });

    // Run controls
    auto run_box = new QGroupBox(tr("Run"), root);
    auto run_layout = new QFormLayout(run_box);
    active_check = new QCheckBox(tr("Active"), run_box);
    run_layout->addRow(active_check);
    auto seed_row = new QHBoxLayout;
    seed_edit = new QLineEdit(run_box);
    auto set_seed = button(tr("Set"), run_box);
    auto reroll = button(tr("Re-roll"), run_box);
    seed_row->addWidget(seed_edit, 1);
    seed_row->addWidget(set_seed);
    seed_row->addWidget(reroll);
    run_layout->addRow(tr("Seed"), seed_row);
    chaos_slider = new IntensitySlider(run_box);
    chaos_slider->setToolTip(tr("Turns every rule up or down together. Below one byte per firing, rules fire less often."));
    chaos_slider->onChanged = [](double v) { CorruptionSession::get().setIntensity(v); };
    run_layout->addRow(tr("Chaos"), chaos_slider);
    status_label = new QLabel(run_box);
    run_layout->addRow(status_label);
    layout->addWidget(run_box);

    connect(active_check, &QCheckBox::clicked, [](bool on) { CorruptionSession::get().setActive(on); });
    connect(reroll, &QPushButton::clicked, [this] {
        seed_edit->setText(QString::number(CorruptionSession::get().reroll()));
    });
    connect(set_seed, &QPushButton::clicked, [this] {
        bool ok = false;
        auto seed = seed_edit->text().trimmed().toULongLong(&ok, 0);
        if (ok)
            CorruptionSession::get().setSeed(seed);
        else
            showMessage(tr("Seed must be a number"), true);
    });

    // Crash guard
    auto guard_box = new QGroupBox(tr("Crash Guard"), root);
    auto guard_layout = new QFormLayout(guard_box);
    detect_check = new QCheckBox(tr("Detect crashes and roll back"), guard_box);
    detect_check->setChecked(true);
    guard_layout->addRow(detect_check);
    policy_combo = new QComboBox(guard_box);
    policy_combo->addItem(tr("Recipe's choice"), QString());
    for (auto p : { PolicyKind::Reroll, PolicyKind::Skip, PolicyKind::Dampen, PolicyKind::Disable, PolicyKind::Escalate })
        policy_combo->addItem(policy_name(p), QString(policy_name(p)));
    guard_layout->addRow(tr("On a crash"), policy_combo);
    crash_label = new QLabel(guard_box);
    crash_label->setWordWrap(true);
    guard_layout->addRow(crash_label);
    auto mark = button(tr("Mark Crash (roll back now)"), guard_box);
    guard_layout->addRow(mark);
    layout->addWidget(guard_box);

    connect(detect_check, &QCheckBox::clicked, [](bool on) { CorruptionSession::get().setDetectionEnabled(on); });
    connect(policy_combo, &QComboBox::activated, [this](int index) {
        auto name = policy_combo->itemData(index).toString().toStdString();
        CorruptionSession::get().setPolicyOverride(name.empty() ? std::nullopt : parse_policy(name));
    });
    connect(mark, &QPushButton::clicked, [] { CorruptionSession::get().markCrash(); });

    // Rules
    rules_table = new QTableWidget(0, 7, root);
    rules_table->setHorizontalHeaderLabels({ tr("On"), tr("Rule"), tr("Target"), tr("Op"), tr("When"), tr("Intensity"), tr("Crashes") });
    rules_table->verticalHeader()->setVisible(false);
    rules_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    rules_table->horizontalHeader()->setStretchLastSection(true);
    rules_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rules_table->setSelectionMode(QAbstractItemView::SingleSelection);
    rules_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(rules_table, 1);
    connect(rules_table, &QTableWidget::cellDoubleClicked, [this](int row, int column) {
        if (column != 0 && column != 5)
            editRule(row);
    });

    auto rule_buttons = new QHBoxLayout;
    add_rule_button = button(tr("Add Rule"), root);
    edit_rule_button = button(tr("Edit"), root);
    duplicate_rule_button = button(tr("Duplicate"), root);
    remove_rule_button = button(tr("Remove"), root);
    rule_buttons->addWidget(add_rule_button);
    rule_buttons->addWidget(edit_rule_button);
    rule_buttons->addWidget(duplicate_rule_button);
    rule_buttons->addWidget(remove_rule_button);
    layout->addLayout(rule_buttons);
    connect(add_rule_button, &QPushButton::clicked, this, &CorruptionDock::addRule);
    connect(edit_rule_button, &QPushButton::clicked, [this] { editRule(selectedRule()); });
    connect(duplicate_rule_button, &QPushButton::clicked, this, &CorruptionDock::duplicateRule);
    connect(remove_rule_button, &QPushButton::clicked, this, &CorruptionDock::removeRule);
    connect(rules_table, &QTableWidget::itemChanged, [this](QTableWidgetItem *item) {
        if (item->column() == 0)
            CorruptionSession::get().setRuleEnabled(size_t(item->row()), item->checkState() == Qt::Checked);
    });

    // Save backups
    auto backup_box = new QGroupBox(tr("Save Backups"), root);
    auto backup_layout = new QVBoxLayout(backup_box);
    backup_list = new QListWidget(backup_box);
    backup_list->setMaximumHeight(120);
    backup_layout->addWidget(backup_list);
    auto backup_buttons = new QHBoxLayout;
    auto restore = button(tr("Restore"), backup_box);
    auto open_backups = button(tr("Open Folder"), backup_box);
    backup_buttons->addWidget(restore);
    backup_buttons->addWidget(open_backups);
    backup_layout->addLayout(backup_buttons);
    layout->addWidget(backup_box);
    connect(restore, &QPushButton::clicked, this, &CorruptionDock::restoreSelectedBackup);
    connect(open_backups, &QPushButton::clicked, [] {
        auto dir = CorruptionSession::get().backupDir();
        QDir().mkpath(qs(dir.string()));
        QDesktopServices::openUrl(QUrl::fromLocalFile(qs(dir.string())));
    });

    message_label = new QLabel(root);
    message_label->setWordWrap(true);
    message_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(message_label);

    setWidget(root);

    connect(&timer, &QTimer::timeout, this, &CorruptionDock::refreshStatus);
    timer.start(250);
    refreshStatus();
}

void CorruptionDock::refreshStatus()
{
    auto status = CorruptionSession::get().status();

    std::string game_key = status.game_loaded ? status.game.sha256 : "";
    if (game_key != shown_game)
    {
        shown_game = game_key;
        game_label->setText(status.game_loaded
                                ? qs(game_folder_name(status.game.title, status.game.sha256))
                                : tr("No game loaded"));
        refreshRecipes();
        refreshBackups();
    }

    if (status.recipe_generation != shown_generation || status.rules_generation != shown_rules_generation)
    {
        shown_generation = status.recipe_generation;
        shown_rules_generation = status.rules_generation;
        rebuildRules();
    }

    chaos_slider->setEnabled(status.has_recipe);
    chaos_slider->refresh(status.intensity);
    for (size_t i = 0; i < rule_sliders.size() && i < status.rules.size(); i++)
        rule_sliders[i]->refresh(status.rules[i].intensity); // a crash-guard dampen moves it

    bool has_selection = selectedRule() >= 0;
    save_button->setEnabled(status.has_recipe && (status.edited || status.new_moment) && !status.recipe_path.empty());
    add_rule_button->setEnabled(status.has_recipe);
    edit_rule_button->setEnabled(has_selection);
    duplicate_rule_button->setEnabled(has_selection);
    remove_rule_button->setEnabled(has_selection);

    QSignalBlocker block(active_check);
    active_check->setEnabled(status.has_recipe);
    active_check->setChecked(status.engine.active);
    if (!seed_edit->hasFocus())
        seed_edit->setText(status.has_recipe ? QString::number(status.engine.seed) : QString());

    status_label->setText(status.has_recipe
                              ? tr("%1%2%3 - frame %4 - %5 firings")
                                    .arg(qs(status.engine.recipe_name))
                                    .arg(status.edited ? tr(" (edited)") : QString())
                                    .arg(status.starting       ? tr(" - starting with the next frame")
                                         : status.new_moment   ? tr(" - new moment, not saved")
                                         : status.has_moment   ? tr(" - from its saved moment")
                                                               : QString())
                                    .arg(status.engine.frame)
                                    .arg(status.engine.firings)
                              : tr("No recipe loaded"));

    crash_label->setText(status.crashes == 0
                             ? tr("No crashes caught. Policy: %1").arg(policy_name(status.policy))
                             : tr("%n crash(es) caught. Last: %1", "", int(status.crashes)).arg(qs(status.last_crash)));
    if (status.rule_crashes != shown_rule_crashes)
    {
        shown_rule_crashes = status.rule_crashes;
        QSignalBlocker block_rules(rules_table);
        for (int i = 0; i < rules_table->rowCount() && i < int(status.rule_crashes.size()); i++)
        {
            rules_table->setItem(i, 6, new QTableWidgetItem(QString::number(status.rule_crashes[size_t(i)])));
            if (rules_table->item(i, 0))
                rules_table->item(i, 0)->setCheckState(status.rules[size_t(i)].enabled ? Qt::Checked : Qt::Unchecked);
        }
    }

    if (status.message != shown_message)
    {
        shown_message = status.message;
        if (message_label->property("source").toString() != "error")
            message_label->setText(qs(status.message));
    }

    if (++ticks % 16 == 0) // every 4 s: new automatic backups show up
        refreshBackups();
}

void CorruptionDock::rebuildRules()
{
    auto status = CorruptionSession::get().status();
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
        rules_table->setItem(i, 0, on);
        rules_table->setItem(i, 1, new QTableWidgetItem(qs(r.name)));
        rules_table->setItem(i, 2, new QTableWidgetItem(qs(describe_range(r))));
        rules_table->setItem(i, 3, new QTableWidgetItem(qs(describe_op(r))));
        rules_table->setItem(i, 4, new QTableWidgetItem(qs(describe_trigger(r.when))));
        unsigned crashes = size_t(i) < status.rule_crashes.size() ? status.rule_crashes[size_t(i)] : 0;
        rules_table->setItem(i, 6, new QTableWidgetItem(QString::number(crashes)));
        auto slider = new IntensitySlider;
        slider->setMinimumWidth(110);
        slider->setIntensity(r.intensity);
        slider->onChanged = [i](double v) { CorruptionSession::get().setRuleIntensity(size_t(i), v); };
        rules_table->setCellWidget(i, 5, slider);
        rule_sliders.push_back(slider);
    }
    selectRule(std::min(selected, rules_table->rowCount() - 1));
}

int CorruptionDock::selectedRule()
{
    auto rows = rules_table->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}

void CorruptionDock::selectRule(int row)
{
    if (row >= 0 && row < rules_table->rowCount())
        rules_table->selectRow(row);
    else
        rules_table->clearSelection();
}

void CorruptionDock::addRule()
{
    auto &session = CorruptionSession::get();
    auto status = session.status();
    if (!status.has_recipe)
    {
        showMessage(tr("Load a recipe or start a new one first"), true);
        return;
    }
    std::vector<std::string> region_errors;
    auto regions = session.library().load_regions(status.game, region_errors);
    SizedMemory sizes;
    sizes.sizes = status.sizes;
    RuleEditor editor(default_rule(), regions, sizes, this);
    editor.setWindowTitle(tr("Add Rule"));
    if (editor.exec() != QDialog::Accepted)
        return;
    auto error = session.addRule(editor.rule());
    if (!error.empty())
    {
        showMessage(tr("Couldn't add the rule: %1").arg(qs(error)), true);
        return;
    }
    rebuildRules();
    selectRule(rules_table->rowCount() - 1);
}

void CorruptionDock::editRule(int row)
{
    auto &session = CorruptionSession::get();
    auto status = session.status();
    if (row < 0 || row >= int(status.rules.size()))
        return;
    std::vector<std::string> region_errors;
    auto regions = session.library().load_regions(status.game, region_errors);
    SizedMemory sizes;
    sizes.sizes = status.sizes;
    RuleEditor editor(status.rules[size_t(row)], regions, sizes, this);
    if (editor.exec() != QDialog::Accepted)
        return;
    auto error = session.replaceRule(size_t(row), editor.rule());
    if (!error.empty())
        showMessage(tr("Couldn't change the rule: %1").arg(qs(error)), true);
    rebuildRules();
    selectRule(row);
}

void CorruptionDock::duplicateRule()
{
    auto &session = CorruptionSession::get();
    auto status = session.status();
    int row = selectedRule();
    if (row < 0 || row >= int(status.rules.size()))
        return;
    Rule copy = status.rules[size_t(row)];
    copy.name += " copy";
    auto error = session.addRule(copy);
    if (!error.empty())
    {
        showMessage(tr("Couldn't duplicate the rule: %1").arg(qs(error)), true);
        return;
    }
    rebuildRules();
    selectRule(rules_table->rowCount() - 1);
}

void CorruptionDock::removeRule()
{
    auto &session = CorruptionSession::get();
    auto status = session.status();
    int row = selectedRule();
    if (row < 0 || row >= int(status.rules.size()))
        return;
    auto name = qs(status.rules[size_t(row)].name);
    if (QMessageBox::question(this, tr("Remove rule"), tr("Remove the rule \"%1\"?").arg(name)) != QMessageBox::Yes)
        return;
    session.removeRule(size_t(row));
    rebuildRules();
    selectRule(row);
}

bool CorruptionDock::confirmDiscard(const QString &action)
{
    auto status = CorruptionSession::get().status();
    bool worth_keeping = status.has_recipe && status.edited && (!status.recipe_path.empty() || !status.rules.empty());
    if (!worth_keeping)
        return true;
    return QMessageBox::question(this, action,
                                 tr("\"%1\" has unsaved changes. Discard them?").arg(qs(status.engine.recipe_name))) ==
           QMessageBox::Yes;
}

void CorruptionDock::newRecipe()
{
    auto status = CorruptionSession::get().status();
    if (!status.game_loaded)
    {
        showMessage(tr("Load a game first"), true);
        return;
    }
    if (!confirmDiscard(tr("New recipe")))
        return;
    bool ok = false;
    auto name = QInputDialog::getText(this, tr("New recipe"), tr("Name:"), QLineEdit::Normal, tr("My recipe"), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    CorruptionSession::get().newRecipe(name.trimmed().toStdString());
    showMessage(tr("New recipe. Add rules, then Save As to keep it."));
}

void CorruptionDock::save()
{
    auto result = CorruptionSession::get().save();
    if (!result.error.empty())
        showMessage(tr("Couldn't save: %1").arg(qs(result.error)), true);
    else
    {
        showMessage(tr("Saved %1").arg(qs(result.path.filename().string())));
        refreshRecipes();
    }
}

void CorruptionDock::refreshRecipes()
{
    auto status = CorruptionSession::get().status();
    recipe_combo->clear();
    entries.clear();
    if (!status.game_loaded)
        return;

    entries = CorruptionSession::get().library().list_recipes(status.game);
    for (auto &e : entries)
    {
        QString label = qs(e.name);
        if (!e.recipe)
            label += tr("  (broken)");
        else if (e.sha_mismatch)
            label += tr("  (other ROM version)");
        recipe_combo->addItem(label);
    }
    if (entries.empty())
        recipe_combo->addItem(tr("No recipes yet: add an example"));
}

void CorruptionDock::refreshBackups()
{
    backup_list->clear();
    for (auto &b : CorruptionSession::get().backups())
    {
        auto when = QDateTime::fromSecsSinceEpoch(
            std::chrono::duration_cast<std::chrono::seconds>(b.time.time_since_epoch()).count());
        auto name = qs(b.path.filename().string());
        auto reason = name.section('_', 2).section('.', 0, 0);
        auto item = new QListWidgetItem(when.toString("yyyy-MM-dd HH:mm:ss") + "  " + reason, backup_list);
        item->setData(Qt::UserRole, qs(b.path.string()));
    }
}

void CorruptionDock::chooseLibrary()
{
    auto dir = QFileDialog::getExistingDirectory(this, tr("Corruption library folder"), library_edit->text());
    if (dir.isEmpty())
        return;
    app.config->corruption_library_root = dir.toStdString();
    CorruptionSession::get().setLibraryRoot(dir.toStdString());
    library_edit->setText(dir);
    refreshRecipes();
    refreshBackups();
}

void CorruptionDock::loadSelectedRecipe()
{
    int index = recipe_combo->currentIndex();
    if (index < 0 || index >= int(entries.size()))
        return;
    auto &entry = entries[size_t(index)];
    if (!confirmDiscard(tr("Load recipe")))
        return;
    if (!entry.recipe)
    {
        showMessage(tr("Can't load %1:\n%2").arg(qs(entry.path.filename().string()), qs(entry.errors.empty() ? "" : entry.errors[0])), true);
        return;
    }
    auto problems = CorruptionSession::get().loadRecipe(*entry.recipe, entry.path);
    if (!problems.empty())
    {
        QStringList lines;
        for (auto &p : problems)
            lines << qs(p);
        showMessage(tr("Recipe doesn't fit this game:\n") + lines.join("\n"), true);
        return;
    }
    showMessage(entry.sha_mismatch ? tr("Loaded. Made for another version of this ROM, so addresses may differ.")
                                   : tr("Loaded %1").arg(qs(entry.name)));
}

void CorruptionDock::saveAs()
{
    auto status = CorruptionSession::get().status();
    if (!status.has_recipe)
    {
        showMessage(tr("Load a recipe first"), true);
        return;
    }
    bool ok = false;
    auto name = QInputDialog::getText(this, tr("Save recipe"), tr("Name:"), QLineEdit::Normal,
                                      qs(status.engine.recipe_name), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    auto result = CorruptionSession::get().saveRecipeAs(name.trimmed().toStdString(), status.notes);
    if (!result.error.empty())
        showMessage(tr("Couldn't save: %1").arg(qs(result.error)), true);
    else
    {
        showMessage(tr("Saved %1").arg(qs(result.path.filename().string())));
        refreshRecipes();
    }
}

void CorruptionDock::addExample()
{
    auto status = CorruptionSession::get().status();
    if (!status.game_loaded)
    {
        showMessage(tr("Load a game first"), true);
        return;
    }
    QFile file(example_combo->currentData().toString());
    if (!file.open(QIODevice::ReadOnly))
        return;
    auto library = CorruptionSession::get().library();
    std::vector<std::string> region_errors;
    auto parsed = parse_recipe(file.readAll().toStdString(), library.load_regions(status.game, region_errors));
    if (!parsed.recipe)
    {
        showMessage(tr("Example is broken: %1").arg(qs(parsed.errors[0])), true);
        return;
    }
    parsed.recipe->game = status.game;
    auto result = library.save_recipe(status.game, *parsed.recipe);
    if (!result.error.empty())
    {
        showMessage(tr("Couldn't add the example: %1").arg(qs(result.error)), true);
        return;
    }
    refreshRecipes();
    for (int i = 0; i < int(entries.size()); i++)
        if (entries[size_t(i)].path == result.path)
            recipe_combo->setCurrentIndex(i);
    showMessage(tr("Added %1 to this game's recipes").arg(qs(parsed.recipe->name)));
}

void CorruptionDock::restoreSelectedBackup()
{
    auto item = backup_list->currentItem();
    if (!item)
    {
        showMessage(tr("Pick a backup first"), true);
        return;
    }
    auto error = CorruptionSession::get().restoreBackup(item->data(Qt::UserRole).toString().toStdString());
    if (!error.empty())
    {
        showMessage(tr("Couldn't restore: %1").arg(qs(error)), true);
        return;
    }
    app.reset();
    refreshBackups();
    showMessage(tr("Save restored and game reset. Your previous save was backed up first."));
}

void CorruptionDock::showMessage(const QString &text, bool error)
{
    message_label->setText(text);
    message_label->setProperty("source", error ? "error" : "info");
    message_label->setStyleSheet(error ? "color: #d33;" : "");
    if (error)
        QTimer::singleShot(8000, this, [this] { message_label->setProperty("source", "info"); message_label->setStyleSheet(""); });
}
