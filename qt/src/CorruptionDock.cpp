#include "CorruptionDock.hpp"
#include "SurvivorsDialog.hpp"
#include "CorruptionSession.hpp"
#include "EmuApplication.hpp"
#include "EmuConfig.hpp"
#include "IntensitySlider.hpp"
#include "MemoryViewer.hpp"
#include "RuleEditor.hpp"
#include "corrupt/rule_text.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QRegularExpression>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>
#include <memory>

using namespace corrupt;

namespace
{

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

// The memory viewer's window onto the running game.
struct SessionMemorySource : MemorySource
{
    CorruptionSession &s = CorruptionSession::get();
    void watch(Target t) override { s.watchMemory(t); }
    void stop() override { s.stopWatching(); }
    void clearHits() override { s.clearHits(); }
    MemorySnapshot snapshot() override { return s.memorySnapshot(); }
    std::vector<CopyRun> findCopies(Target of) override { return s.findCopies(of); }
    std::vector<MapArea> memoryMap(Target t) override { return s.memoryMap(t); }
    std::vector<uint8_t> palette() override { return s.palette(); }
    std::array<size_t, kTargetCount> sizes() override { return s.status().sizes; }
    Regions regions() override { return s.regions(); }
    std::string addRule(const Rule &rule) override { return s.addRule(rule); }
    std::string paint(Target t, const std::vector<uint32_t> &offsets, uint8_t value) override
    {
        return s.paint(t, offsets, value);
    }
    std::string setBytes(Target t, const std::vector<std::pair<uint32_t, uint8_t>> &bytes) override
    {
        return s.setBytes(t, bytes);
    }
    std::string saveRegion(const std::string &name, const AddressRange &range) override { return s.saveRegion(name, range); }
    std::string renameRegion(const std::string &from, const std::string &to) override { return s.renameRegion(from, to); }
    std::string deleteRegion(const std::string &name) override { return s.deleteRegion(name); }
    std::vector<std::string> regionUsers(const std::string &name) override { return s.regionUsers(name); }
};

struct SessionSurvivors : SurvivorSource
{
    CorruptionSession &s = CorruptionSession::get();
    std::vector<Survivor> survivors() override { return s.survivors(); }
    std::vector<Pin> pins() override { return s.pins(); }
    std::string pin(const std::vector<uint32_t> &offsets, const std::string &note) override { return s.pin(offsets, note); }
    void unpin(const std::vector<uint32_t> &offsets) override { s.unpin(offsets); }
};

QPushButton *button(const QString &text, QWidget *parent)
{
    auto b = new QPushButton(text, parent);
    b->setAutoDefault(false);
    return b;
}

// Helper text: quieter than the controls, but not greyed out like something disabled.
void hint(QLabel *label)
{
    auto pal = label->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::PlaceholderText));
    label->setPalette(pal);
}

QString plural(qulonglong n, const QString &one, const QString &many)
{
    return n == 1 ? one : many.arg(QLocale().toString(n));
}

} // namespace

QString CorruptionDock::hotkey(int shortcut) const
{
    for (int i = 0; i < EmuConfig::allowed_bindings; i++)
    {
        auto b = app.config->binding.shortcuts[shortcut * EmuConfig::allowed_bindings + i];
        if (b.type != EmuBinding::None)
            return qs(b.to_string());
    }
    return tr("no hotkey");
}

QString CorruptionDock::policyText(PolicyKind p)
{
    switch (p)
    {
    case PolicyKind::Reroll: return tr("Re-roll the guilty rule's dice");
    case PolicyKind::Skip: return tr("Skip the guilty rule's last changes");
    case PolicyKind::Dampen: return tr("Halve the guilty rule's strength");
    case PolicyKind::Disable: return tr("Turn the guilty rule off");
    case PolicyKind::Escalate: return tr("Re-roll twice, then halve, then turn off");
    }
    return {};
}

CorruptionDock::CorruptionDock(EmuApplication &app, QWidget *parent)
    : QDockWidget(tr("Corruption"), parent), app(app)
{
    setObjectName("CorruptionDock");
    auto root = new QWidget(this);
    auto layout = new QVBoxLayout(root);

    // The game, and the guide.
    auto top_row = new QHBoxLayout;
    game_label = new QLabel(tr("No game loaded"), root);
    game_label->setObjectName("game");
    game_label->setWordWrap(true);
    QFont title_font = game_label->font();
    title_font.setBold(true);
    game_label->setFont(title_font);
    top_row->addWidget(game_label, 1);
    guide_button = button(tr("? Guide"), root);
    guide_button->setObjectName("guide");
    guide_button->setToolTip(tr("How everything here works, in plain words"));
    top_row->addWidget(guide_button);
    layout->addLayout(top_row);

    // While nothing is loaded: where to start.
    start_card = new QLabel(root);
    start_card->setObjectName("start_here");
    start_card->setWordWrap(true);
    start_card->setFrameShape(QFrame::StyledPanel);
    start_card->setMargin(8);
    start_card->setText(tr("<b>Start here.</b> 1. Pick a recipe below: the <i>starters</i> come with the app. "
                           "2. Press <b>Corrupt</b> (%1) and watch the game. 3. <b>Back to normal</b> (%2) "
                           "undoes everything. <b>? Guide</b> explains the rest.")
                            .arg(hotkey(EmuConfig::eCorruptionToggle), hotkey(EmuConfig::eCorruptionBackToNormal)));
    layout->addWidget(start_card);

    // Recipe: what to corrupt.
    auto recipe_box = new QGroupBox(tr("Recipe"), root);
    auto recipe_layout = new QVBoxLayout(recipe_box);
    auto recipe_row = new QHBoxLayout;
    recipe_combo = new QComboBox(recipe_box);
    recipe_combo->setObjectName("recipe");
    recipe_combo->setToolTip(tr("Your recipes for this game, then the starters that come with the app. Choosing "
                                "one loads it."));
    recipe_row->addWidget(recipe_combo, 1);
    auto new_recipe = button(tr("New"), recipe_box);
    new_recipe->setToolTip(tr("Start an empty recipe for this game"));
    save_button = button(tr("Save"), recipe_box);
    save_button->setObjectName("save");
    save_button->setToolTip(tr("Save the recipe (with its pins, and its start moment if that's on) in the library"));
    auto more = new QToolButton(recipe_box);
    more->setObjectName("recipe_more");
    more->setText(tr("More"));
    more->setPopupMode(QToolButton::InstantPopup);
    auto menu = new QMenu(more);
    auto save_as = menu->addAction(tr("Save As…"));
    auto share = menu->addAction(tr("Share…"));
    share->setToolTip(tr("One file with the recipe, its pins and its start moment, to send to a friend"));
    auto import = menu->addAction(tr("Import a Shared Recipe…"));
    menu->setToolTipsVisible(true);
    menu->addSeparator();
    auto open_folder = menu->addAction(tr("Open This Game's Recipe Folder"));
    auto rescan = menu->addAction(tr("Look for New Recipe Files"));
    auto choose_library = menu->addAction(tr("Choose the Library Folder…"));
    more->setMenu(menu);
    recipe_row->addWidget(new_recipe);
    recipe_row->addWidget(save_button);
    recipe_row->addWidget(more);
    recipe_layout->addLayout(recipe_row);
    moments_check = new QCheckBox(tr("Remember where each run starts"), recipe_box);
    moments_check->setObjectName("moments");
    moments_check->setToolTip(tr("Save the moment a run started (a save state) with the recipe, and go back to it "
                                 "when the recipe is loaded, so the same seed plays out the same way."));
    moments_check->setChecked(app.config->corruption_saved_moments);
    recipe_layout->addWidget(moments_check);
    library_label = new QLabel(recipe_box);
    library_label->setObjectName("library");
    library_label->setWordWrap(true);
    hint(library_label);
    recipe_layout->addWidget(library_label);
    layout->addWidget(recipe_box);

    for (auto &file : QDir(":/corruption-examples").entryList({ "*.json" }, QDir::Files, QDir::Name))
    {
        QFile f(":/corruption-examples/" + file);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        if (auto parsed = parse_recipe(f.readAll().toStdString(), {}); parsed.recipe)
            starters.push_back({ ":/corruption-examples/" + file, *parsed.recipe });
    }

    connect(moments_check, &QCheckBox::toggled, [this](bool on) {
        this->app.config->corruption_saved_moments = on;
        CorruptionSession::get().setSavedMoments(on);
    });
    connect(recipe_combo, &QComboBox::activated, this, &CorruptionDock::recipeChosen);
    connect(new_recipe, &QPushButton::clicked, this, &CorruptionDock::newRecipe);
    connect(save_button, &QPushButton::clicked, this, &CorruptionDock::save);
    connect(save_as, &QAction::triggered, this, &CorruptionDock::saveAs);
    connect(share, &QAction::triggered, [this] {
        auto status = CorruptionSession::get().status();
        if (!status.has_recipe)
            return showMessage(tr("Load a recipe first"), true);
        QString name = qs(status.engine.recipe_name).trimmed().replace(QRegularExpression("[^A-Za-z0-9 _-]"), "_");
        auto path = QFileDialog::getSaveFileName(this, tr("Share recipe"), QDir::home().filePath(name + ".s9xrecipe"),
                                                 tr("Shared recipe (*.s9xrecipe)"));
        if (path.isEmpty())
            return;
        auto err = CorruptionSession::get().shareRecipe(path.toStdString());
        showMessage(err.empty() ? tr("Saved %1. Send it to a friend: they choose More > Import.").arg(path) : qs(err),
                    !err.empty());
    });
    connect(import, &QAction::triggered, [this] {
        if (!confirmDiscard(tr("Import recipe")))
            return;
        auto path = QFileDialog::getOpenFileName(this, tr("Import a shared recipe"), QDir::homePath(),
                                                 tr("Shared recipe (*.s9xrecipe)"));
        if (path.isEmpty())
            return;
        std::string name;
        auto err = CorruptionSession::get().importRecipe(path.toStdString(), name);
        refreshRecipes();
        showMessage(err.empty() ? tr("Imported %1 into this game's recipes. Press Corrupt to try it.").arg(qs(name))
                                : tr("Couldn't import: %1").arg(qs(err)),
                    !err.empty());
    });
    connect(rescan, &QAction::triggered, this, &CorruptionDock::refreshRecipes);
    connect(choose_library, &QAction::triggered, this, &CorruptionDock::chooseLibrary);
    connect(open_folder, &QAction::triggered, [this] {
        auto status = CorruptionSession::get().status();
        if (!status.game_loaded)
            return showMessage(tr("Load a game first"), true);
        auto dir = CorruptionSession::get().library().game_dir(status.game) / "recipes";
        QDir().mkpath(qs(dir.string()));
        QDesktopServices::openUrl(QUrl::fromLocalFile(qs(dir.string())));
    });

    // Corrupt: on, off, back to normal.
    auto run_box = new QGroupBox(tr("Corrupt"), root);
    auto run_layout = new QVBoxLayout(run_box);
    auto run_row = new QHBoxLayout;
    corrupt_button = button(tr("Corrupt (%1)").arg(hotkey(EmuConfig::eCorruptionToggle)), run_box);
    corrupt_button->setObjectName("corrupt");
    corrupt_button->setCheckable(true);
    corrupt_button->setMinimumHeight(34);
    corrupt_button->setToolTip(tr("Turn the recipe on or off. Off keeps what it already changed; Back to normal "
                                  "undoes it. Hotkey: %1").arg(hotkey(EmuConfig::eCorruptionToggle)));
    back_button = button(tr("Back to Normal (%1)").arg(hotkey(EmuConfig::eCorruptionBackToNormal)), run_box);
    back_button->setObjectName("back_to_normal");
    back_button->setMinimumHeight(34);
    back_button->setToolTip(tr("Turn corruption off, put the ROM back to clean, and take the game back to the "
                               "moment the run started, so everything it changed is gone. Hotkey: %1")
                                .arg(hotkey(EmuConfig::eCorruptionBackToNormal)));
    run_row->addWidget(corrupt_button, 1);
    run_row->addWidget(back_button, 1);
    run_layout->addLayout(run_row);
    status_label = new QLabel(run_box);
    status_label->setObjectName("run_status");
    status_label->setWordWrap(true);
    run_layout->addWidget(status_label);
    auto run_form = new QFormLayout;
    chaos_slider = new IntensitySlider(run_box);
    chaos_slider->setObjectName("chaos");
    chaos_slider->setToolTip(tr("How hard the whole recipe hits: turns every rule up or down together. Hotkeys: "
                                "%1 more, %2 less.")
                                 .arg(hotkey(EmuConfig::eCorruptionMore), hotkey(EmuConfig::eCorruptionLess)));
    chaos_slider->onChanged = [](double v) { CorruptionSession::get().setIntensity(v); };
    run_form->addRow(tr("Chaos"), chaos_slider);
    auto seed_row = new QHBoxLayout;
    seed_edit = new QLineEdit(run_box);
    seed_edit->setObjectName("seed");
    seed_edit->setToolTip(tr("The dice. The same recipe with the same seed, from the same start, does the same "
                             "thing every time. Type one and press Enter to use it."));
    auto reroll = button(tr("Re-roll (%1)").arg(hotkey(EmuConfig::eCorruptionReroll)), run_box);
    reroll->setObjectName("reroll");
    reroll->setToolTip(tr("New dice: same recipe, different corruption, starting over. Hotkey: %1")
                           .arg(hotkey(EmuConfig::eCorruptionReroll)));
    seed_row->addWidget(seed_edit, 1);
    seed_row->addWidget(reroll);
    run_form->addRow(tr("Seed"), seed_row);
    run_layout->addLayout(run_form);
    layout->addWidget(run_box);

    connect(corrupt_button, &QPushButton::clicked, [](bool on) { CorruptionSession::get().setActive(on); });
    connect(back_button, &QPushButton::clicked, [] { CorruptionSession::get().backToNormal(); });
    connect(reroll, &QPushButton::clicked, [this] {
        seed_edit->setText(QString::number(CorruptionSession::get().reroll()));
    });
    connect(seed_edit, &QLineEdit::returnPressed, [this] {
        bool ok = false;
        auto seed = seed_edit->text().trimmed().toULongLong(&ok, 0);
        if (ok)
            CorruptionSession::get().setSeed(seed);
        else
            showMessage(tr("The seed must be a number"), true);
    });

    // Rules
    auto rules_box = new QGroupBox(tr("Rules"), root);
    auto rules_layout = new QVBoxLayout(rules_box);
    rules_table = new QTableWidget(0, 7, rules_box);
    rules_table->setObjectName("rules");
    rules_table->setHorizontalHeaderLabels(
        { tr("On"), tr("Rule"), tr("Memory"), tr("What"), tr("When"), tr("Strength"), tr("Crashes") });
    rules_table->horizontalHeaderItem(5)->setToolTip(tr("How hard this rule hits, on top of Chaos"));
    rules_table->horizontalHeaderItem(6)->setToolTip(tr("How often the crash guard blamed this rule"));
    rules_table->verticalHeader()->setVisible(false);
    rules_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    rules_table->horizontalHeader()->setStretchLastSection(true);
    rules_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rules_table->setSelectionMode(QAbstractItemView::SingleSelection);
    rules_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    rules_table->setToolTip(tr("Each rule changes some memory now and then. Double-click one to edit it."));
    rules_layout->addWidget(rules_table, 1);
    rules_empty = new QLabel(tr("No rules yet. Add Rule, or pick a starter in the recipe list above."), rules_box);
    rules_empty->setObjectName("rules_empty");
    rules_empty->setWordWrap(true);
    hint(rules_empty);
    rules_layout->addWidget(rules_empty);
    connect(rules_table, &QTableWidget::cellDoubleClicked, [this](int row, int column) {
        if (column != 0 && column != 5)
            editRule(row);
    });

    auto rule_buttons = new QHBoxLayout;
    add_rule_button = button(tr("Add Rule"), rules_box);
    add_rule_button->setToolTip(tr("A new rule: which memory, what to do to it, and when"));
    edit_rule_button = button(tr("Edit"), rules_box);
    duplicate_rule_button = button(tr("Duplicate"), rules_box);
    remove_rule_button = button(tr("Remove"), rules_box);
    auto viewer_button = button(tr("Memory Viewer…"), rules_box);
    viewer_button->setObjectName("memory_viewer");
    viewer_button->setToolTip(tr("Look inside the game's memory: what changes, what the recipe hits, where the "
                                 "code is. Select bytes there to make a rule from them."));
    rule_buttons->addWidget(add_rule_button);
    rule_buttons->addWidget(edit_rule_button);
    rule_buttons->addWidget(duplicate_rule_button);
    rule_buttons->addWidget(remove_rule_button);
    rule_buttons->addStretch(1);
    rule_buttons->addWidget(viewer_button);
    rules_layout->addLayout(rule_buttons);
    layout->addWidget(rules_box, 1);
    connect(add_rule_button, &QPushButton::clicked, this, &CorruptionDock::addRule);
    connect(edit_rule_button, &QPushButton::clicked, [this] { editRule(selectedRule()); });
    connect(duplicate_rule_button, &QPushButton::clicked, this, &CorruptionDock::duplicateRule);
    connect(remove_rule_button, &QPushButton::clicked, this, &CorruptionDock::removeRule);
    connect(viewer_button, &QPushButton::clicked, this, &CorruptionDock::openViewer);
    connect(rules_table, &QTableWidget::itemChanged, [this](QTableWidgetItem *item) {
        if (item->column() == 0)
            CorruptionSession::get().setRuleEnabled(size_t(item->row()), item->checkState() == Qt::Checked);
    });

    // Crash guard
    auto guard_box = new QGroupBox(tr("Crash Guard"), root);
    auto guard_layout = new QFormLayout(guard_box);
    detect_check = new QCheckBox(tr("Catch crashes and go back a few seconds"), guard_box);
    detect_check->setObjectName("detect");
    detect_check->setToolTip(tr("Notices when the game freezes, goes black, stops or reboots itself, takes it back "
                                "to a moment before, and changes what caused it so it plays on."));
    detect_check->setChecked(true);
    guard_layout->addRow(detect_check);
    policy_combo = new QComboBox(guard_box);
    policy_combo->setObjectName("policy");
    policy_combo->addItem(tr("Whatever the recipe says"), QString());
    for (auto p : { PolicyKind::Reroll, PolicyKind::Skip, PolicyKind::Dampen, PolicyKind::Disable, PolicyKind::Escalate })
        policy_combo->addItem(policyText(p), QString(policy_name(p)));
    policy_combo->setToolTip(tr("What to do with the rule that caused a crash. Changed ROM bytes the game used "
                                "are put back either way."));
    guard_layout->addRow(tr("Then"), policy_combo);
    crash_label = new QLabel(guard_box);
    crash_label->setObjectName("crash_status");
    crash_label->setWordWrap(true);
    guard_layout->addRow(crash_label);
    auto mark = button(tr("It Crashed: Go Back Now"), guard_box);
    mark->setObjectName("mark_crash");
    mark->setToolTip(tr("For a crash the guard missed: go back about two seconds and blame what changed. Hotkey: %1")
                         .arg(hotkey(EmuConfig::eCorruptionMarkCrash)));
    guard_layout->addRow(mark);
    layout->addWidget(guard_box);

    connect(detect_check, &QCheckBox::clicked, [](bool on) { CorruptionSession::get().setDetectionEnabled(on); });
    connect(policy_combo, &QComboBox::activated, [this](int index) {
        auto name = policy_combo->itemData(index).toString().toStdString();
        CorruptionSession::get().setPolicyOverride(name.empty() ? std::nullopt : parse_policy(name));
    });
    connect(mark, &QPushButton::clicked, [] { CorruptionSession::get().markCrash(); });

    // The ROM: the game itself.
    auto rom_box = new QGroupBox(tr("ROM (the Game Itself)"), root);
    auto rom_layout = new QVBoxLayout(rom_box);
    rom_label = new QLabel(rom_box);
    rom_label->setObjectName("rom_status");
    rom_label->setWordWrap(true);
    rom_layout->addWidget(rom_label);
    auto rom_row = new QHBoxLayout;
    survivors_button = button(tr("Survivors…"), rom_box);
    survivors_button->setObjectName("survivors");
    survivors_button->setToolTip(tr("ROM changes the game keeps using without crashing. Pin the ones you like to "
                                    "keep them in the recipe."));
    clear_paint_button = button(tr("Clear Paint"), rom_box);
    clear_paint_button->setObjectName("clear_paint");
    clear_paint_button->setToolTip(tr("Forget the bytes painted in the memory viewer. They stay until the next run "
                                      "starts."));
    restore_rom_button = button(tr("Restore Clean ROM"), rom_box);
    restore_rom_button->setObjectName("restore_rom");
    restore_rom_button->setToolTip(tr("Undo every change to the ROM now, and leave RAM as it is"));
    auto export_rom = button(tr("Export ROM…"), rom_box);
    export_rom->setObjectName("export_rom");
    export_rom->setToolTip(tr("Save the ROM as it is now as a .sfc file (checksum fixed), for other emulators or "
                              "a flash cart"));
    rom_row->addWidget(survivors_button);
    rom_row->addWidget(clear_paint_button);
    rom_row->addWidget(restore_rom_button);
    rom_row->addWidget(export_rom);
    rom_row->addStretch(1);
    rom_layout->addLayout(rom_row);
    layout->addWidget(rom_box);
    connect(restore_rom_button, &QPushButton::clicked, [] { CorruptionSession::get().restoreCleanRom(); });
    connect(clear_paint_button, &QPushButton::clicked, [] { CorruptionSession::get().clearPatches(); });
    connect(survivors_button, &QPushButton::clicked, [this] {
        if (!survivors_dialog)
        {
            survivor_source = std::make_unique<SessionSurvivors>();
            survivors_dialog = new SurvivorsDialog(*survivor_source, this);
        }
        survivors_dialog->refresh();
        survivors_dialog->show();
        survivors_dialog->raise();
        survivors_dialog->activateWindow();
    });
    connect(export_rom, &QPushButton::clicked, this, &CorruptionDock::exportRom);

    // Game save backups
    auto backup_box = new QGroupBox(tr("Game Save Backups"), root);
    auto backup_layout = new QVBoxLayout(backup_box);
    auto backup_about = new QLabel(tr("Your in-game saves (the cartridge's battery save), copied automatically "
                                      "while you play. This doesn't undo corruption: Back to normal does."),
                                   backup_box);
    backup_about->setWordWrap(true);
    hint(backup_about);
    backup_layout->addWidget(backup_about);
    backup_list = new QListWidget(backup_box);
    backup_list->setObjectName("backups");
    backup_list->setMaximumHeight(100);
    backup_layout->addWidget(backup_list);
    auto backup_buttons = new QHBoxLayout;
    auto restore = button(tr("Restore This Save…"), backup_box);
    restore->setObjectName("restore_backup");
    restore->setToolTip(tr("Replace the game's save with the selected copy and restart the game. Your current "
                           "save is copied first."));
    auto open_backups = button(tr("Open Folder"), backup_box);
    backup_buttons->addWidget(restore);
    backup_buttons->addWidget(open_backups);
    backup_buttons->addStretch(1);
    backup_layout->addLayout(backup_buttons);
    layout->addWidget(backup_box);
    connect(restore, &QPushButton::clicked, this, &CorruptionDock::restoreSelectedBackup);
    connect(open_backups, &QPushButton::clicked, [] {
        auto dir = CorruptionSession::get().backupDir();
        QDir().mkpath(qs(dir.string()));
        QDesktopServices::openUrl(QUrl::fromLocalFile(qs(dir.string())));
    });

    message_label = new QLabel(root);
    message_label->setObjectName("message");
    message_label->setWordWrap(true);
    message_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(message_label);

    if (auto dir = qgetenv("SNES9X_UI_TOUR"); !dir.isEmpty())
        QTimer::singleShot(4000, this, [this, dir] { tour(QString::fromLocal8Bit(dir)); });
    setWidget(root);

    connect(&timer, &QTimer::timeout, this, &CorruptionDock::refreshStatus);
    timer.start(250);
    refreshStatus();
}

void CorruptionDock::openViewer()
{
    if (!memory_viewer)
    {
        memory_source = std::make_unique<SessionMemorySource>();
        memory_viewer = new MemoryViewer(*memory_source, this);
    }
    memory_viewer->show();
    memory_viewer->raise();
    memory_viewer->activateWindow();
}

void CorruptionDock::tour(const QString &dir)
{
    QDir().mkpath(dir);
    auto shot = [dir](QWidget *w, const QString &name) {
        QApplication::processEvents();
        w->grab().save(QString("%1/%2.png").arg(dir, name));
    };
    auto choose = [](QWidget *root, const char *name, const QString &data) {
        if (auto c = root->findChild<QComboBox *>(name))
            if (int i = c->findData(data); i >= 0)
                c->setCurrentIndex(i);
    };
    // Each step waits for the game to run a little, so the pictures show live data.
    auto steps = std::make_shared<std::vector<std::function<void()>>>();
    auto next = std::make_shared<std::function<void()>>();
    *next = [steps, next] {
        if (steps->empty())
            return;
        auto step = steps->front();
        steps->erase(steps->begin());
        step();
        QTimer::singleShot(1500, *next);
    };
    steps->push_back([=, this] {
        setVisible(true);
        shot(window(), "01-window");
        shot(this, "02-panel-no-recipe");
    });
    steps->push_back([=, this] {
        QFile file(":/corruption-examples/low-wram-roulette.json");
        if (file.open(QIODevice::ReadOnly))
            if (auto parsed = parse_recipe(file.readAll().toStdString(), {}); parsed.recipe)
            {
                CorruptionSession::get().loadRecipe(*parsed.recipe);
                CorruptionSession::get().setActive(true);
            }
    });
    steps->push_back([=, this] {
        refreshStatus();
        shot(this, "03-panel-running");
        auto status = CorruptionSession::get().status();
        if (!status.rules.empty())
        {
            SizedMemory sizes;
            RuleEditor editor(status.rules[0], CorruptionSession::get().regions(), sizes, this);
            editor.show();
            editor.adjustSize();
            shot(&editor, "04-rule-editor");
        }
        openViewer();
    });
    steps->push_back([=, this] {
        // Share and import, end to end, and Back to normal.
        QFile log(dir + "/flows.txt");
        log.open(QIODevice::WriteOnly | QIODevice::Text);
        auto share = CorruptionSession::get().shareRecipe((dir + "/tour.s9xrecipe").toStdString());
        std::string name;
        auto import = CorruptionSession::get().importRecipe((dir + "/tour.s9xrecipe").toStdString(), name);
        log.write(QString("share: %1\nimport: %2 as %3\n")
                      .arg(qs(share.empty() ? "ok" : share), qs(import.empty() ? "ok" : import), qs(name))
                      .toUtf8());
        refreshRecipes();
        CorruptionSession::get().setActive(true);
        shot(memory_viewer, "05-viewer-wram-hex");
    });
    steps->push_back([=, this] { CorruptionSession::get().backToNormal(); });
    steps->push_back([=, this] {
        refreshStatus();
        shot(this, "05b-panel-back-to-normal");
    });
    steps->push_back([=, this] {
        choose(memory_viewer, "mode", "pixels");
        choose(memory_viewer, "overlay", "activity");
    });
    steps->push_back([=, this] { shot(memory_viewer, "06-viewer-wram-pixels"); });
    steps->push_back([=, this] {
        choose(memory_viewer, "target", "rom");
        choose(memory_viewer, "overlay", "code");
    });
    steps->push_back([=, this] { shot(memory_viewer, "07-viewer-rom-code"); });
    steps->push_back([=, this] {
        choose(memory_viewer, "target", "vram");
        choose(memory_viewer, "mode", "tiles");
    });
    steps->push_back([=, this] {
        shot(memory_viewer, "08-viewer-vram-tiles");
        memory_viewer->close();
        survivors_button->click();
    });
    steps->push_back([=, this] {
        if (survivors_dialog)
        {
            shot(survivors_dialog, "09-survivors");
            survivors_dialog->close();
        }
        QFile done(dir + "/done");
        done.open(QIODevice::WriteOnly);
    });
    (*next)();
}

void CorruptionDock::exportRom()
{
    auto status = CorruptionSession::get().status();
    if (!status.game_loaded)
        return;
    QString name = qs(status.game.title).trimmed().replace(QRegularExpression("[^A-Za-z0-9 _-]"), "_") +
                   (status.rom_changed ? tr(" (corrupted %1)").arg(status.engine.seed) : QString()) + ".sfc";
    auto path = QFileDialog::getSaveFileName(this, tr("Export ROM"), QDir::home().filePath(name),
                                             tr("SNES ROM (*.sfc *.smc)"));
    if (path.isEmpty())
        return;
    auto err = CorruptionSession::get().exportRom(path.toStdString());
    showMessage(err.empty() ? tr("Exported %1").arg(path) : qs(err), !err.empty());
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

    if (status.recipe_generation != shown_choice_generation || status.recipe_path != shown_recipe_path)
    {
        shown_choice_generation = status.recipe_generation;
        shown_recipe_path = status.recipe_path;
        syncRecipeChoice();
    }
    start_card->setVisible(status.game_loaded && !status.has_recipe);
    library_label->setText(tr("Library: %1").arg(qs(app.config->corruption_library_root)));

    chaos_slider->setEnabled(status.has_recipe);
    chaos_slider->refresh(status.intensity);
    for (size_t i = 0; i < rule_sliders.size() && i < status.rules.size(); i++)
        rule_sliders[i]->refresh(status.rules[i].intensity); // a crash-guard dampen moves it

    bool has_selection = selectedRule() >= 0;
    save_button->setEnabled(status.has_recipe && (status.edited || status.new_moment || status.recipe_path.empty()));
    add_rule_button->setEnabled(status.has_recipe);
    edit_rule_button->setEnabled(has_selection);
    duplicate_rule_button->setEnabled(has_selection);
    remove_rule_button->setEnabled(has_selection);
    rules_empty->setVisible(status.has_recipe ? status.rules.empty() : false);

    {
        QSignalBlocker block(corrupt_button);
        corrupt_button->setEnabled(status.has_recipe);
        corrupt_button->setChecked(status.engine.active);
        corrupt_button->setText(status.engine.active ? tr("Corrupting… (%1 to stop)").arg(hotkey(EmuConfig::eCorruptionToggle))
                                                     : tr("Corrupt (%1)").arg(hotkey(EmuConfig::eCorruptionToggle)));
        corrupt_button->setToolTip(status.has_recipe ? tr("Turn the recipe on or off. Off keeps what it already "
                                                          "changed; Back to normal undoes it.")
                                                     : tr("Pick a recipe first"));
    }
    back_button->setEnabled(status.can_go_back || status.engine.active);
    if (!seed_edit->hasFocus())
        seed_edit->setText(status.has_recipe ? QString::number(status.engine.seed) : QString());

    // What's happening, in words.
    QString run;
    if (!status.game_loaded)
        run = tr("Load a game to start.");
    else if (!status.has_recipe)
        run = tr("No recipe loaded: pick one above.");
    else if (status.starting)
        run = tr("Starting with the next frame…");
    else if (status.engine.active)
    {
        run = tr("On for %1 s · %2").arg(status.engine.frame / 60).arg(plural(status.engine.firings, tr("1 change made"), tr("%1 changes made")));
        if (status.crashes)
            run += " · " + plural(status.crashes, tr("1 crash caught"), tr("%1 crashes caught"));
        if (status.new_moment)
            run += tr(" · start moment not saved yet");
        else if (status.has_moment)
            run += tr(" · started from its saved moment");
    }
    else if (status.back_to_normal)
        run = tr("Off. The game is back to normal.");
    else if (status.can_go_back)
        run = tr("Off. What it changed stays until you press Back to normal.");
    else
        run = tr("Off. Press Corrupt to start.");
    if (status.edited && !status.recipe_path.empty())
        run += tr(" · the recipe has unsaved changes");
    status_label->setText(run);

    // The ROM, in words.
    QStringList rom_lines;
    if (status.game_loaded)
    {
        rom_lines << (status.rom_changed == 0
                          ? tr("Clean: nothing has changed the game itself.")
                          : tr("%1, %2 of them used by the game so far.")
                                .arg(plural(status.rom_changed, tr("1 byte changed"), tr("%1 bytes changed")))
                                .arg(QLocale().toString(qulonglong(status.rom_used))));
        QStringList kept;
        if (status.survivors)
            kept << plural(status.survivors, tr("1 survivor"), tr("%1 survivors"));
        if (status.pins)
            kept << plural(status.pins, tr("1 pinned"), tr("%1 pinned"));
        if (status.patched)
            kept << plural(status.patched, tr("1 byte painted"), tr("%1 bytes painted"));
        if (!kept.isEmpty())
            rom_lines << kept.join(" · ");
        rom_lines << (status.code.seen
                          ? tr("Known so far: %1 bytes of code, %2 of data, %3 of graphics (it learns as you play).")
                                .arg(QLocale().toString(qulonglong(status.code.code)),
                                     QLocale().toString(qulonglong(status.code.data)),
                                     QLocale().toString(qulonglong(status.code.dma)))
                          : tr("Nothing known about its code yet: it learns as you play."));
    }
    rom_label->setText(rom_lines.join("\n"));
    restore_rom_button->setEnabled(status.rom_changed > 0);
    clear_paint_button->setEnabled(status.patched > 0);

    QString learned = status.hot_blocks == 0
                          ? QString()
                          : "\n" + plural(status.hot_blocks, tr("It has learned 1 dangerous spot (rules can avoid it)."),
                                          tr("It has learned %1 dangerous spots (rules can avoid them)."));
    crash_label->setText((status.crashes == 0 ? tr("No crashes yet.")
                                              : plural(status.crashes, tr("1 crash caught."), tr("%1 crashes caught.")) +
                                                    tr(" Last: %1").arg(qs(status.last_crash))) +
                         learned);
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
    showMessage(tr("New recipe. Add a rule, press Corrupt to try it, and Save to keep it."));
}

void CorruptionDock::save()
{
    if (CorruptionSession::get().status().recipe_path.empty())
        return saveAs(); // a new recipe or a starter: it needs a name first
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
    QSignalBlocker block(recipe_combo);
    recipe_combo->clear();
    entries.clear();
    if (status.game_loaded)
        entries = CorruptionSession::get().library().list_recipes(status.game);
    recipe_combo->addItem(tr("Choose a recipe…"), QString("none"));
    for (size_t i = 0; i < entries.size(); i++)
    {
        auto &e = entries[i];
        QString label = qs(e.name);
        if (!e.recipe)
            label += tr("  (broken)");
        else if (e.sha_mismatch)
            label += tr("  (made for another version of this ROM)");
        recipe_combo->addItem(label, QString("mine:%1").arg(i));
        if (e.recipe && !e.recipe->notes.empty())
            recipe_combo->setItemData(recipe_combo->count() - 1, qs(e.recipe->notes), Qt::ToolTipRole);
    }
    recipe_combo->insertSeparator(recipe_combo->count());
    for (size_t i = 0; i < starters.size(); i++)
    {
        recipe_combo->addItem(tr("Starter: %1").arg(qs(starters[i].recipe.name)), QString("starter:%1").arg(i));
        recipe_combo->setItemData(recipe_combo->count() - 1, qs(starters[i].recipe.notes), Qt::ToolTipRole);
    }
    syncRecipeChoice();
}

void CorruptionDock::syncRecipeChoice()
{
    auto status = CorruptionSession::get().status();
    QSignalBlocker block(recipe_combo);
    // A recipe that's loaded but isn't in the list (new, or a starter) gets a line of its own.
    for (int i = recipe_combo->count() - 1; i >= 0; i--)
        if (recipe_combo->itemData(i).toString() == "loaded")
            recipe_combo->removeItem(i);
    if (!status.has_recipe)
        return recipe_combo->setCurrentIndex(0);
    for (size_t i = 0; i < entries.size(); i++)
        if (!status.recipe_path.empty() && entries[i].path == status.recipe_path)
            return recipe_combo->setCurrentIndex(recipe_combo->findData(QString("mine:%1").arg(i)));
    recipe_combo->insertItem(1, tr("%1 (not saved yet)").arg(qs(status.engine.recipe_name)), QString("loaded"));
    recipe_combo->setCurrentIndex(1);
}

void CorruptionDock::recipeChosen(int index)
{
    auto choice = recipe_combo->itemData(index).toString();
    auto status = CorruptionSession::get().status();
    if (choice == "none" || choice == "loaded" || choice.isEmpty())
        return syncRecipeChoice();
    if (!status.game_loaded)
    {
        showMessage(tr("Load a game first"), true);
        return syncRecipeChoice();
    }
    if (!confirmDiscard(tr("Load recipe")))
        return syncRecipeChoice();
    std::vector<std::string> problems;
    QString loaded;
    bool other_version = false;
    if (choice.startsWith("mine:"))
    {
        auto &entry = entries[size_t(choice.mid(5).toInt())];
        if (!entry.recipe)
        {
            showMessage(tr("Can't load %1:\n%2").arg(qs(entry.path.filename().string()), qs(entry.errors.empty() ? "" : entry.errors[0])), true);
            return syncRecipeChoice();
        }
        problems = CorruptionSession::get().loadRecipe(*entry.recipe, entry.path);
        loaded = qs(entry.name);
        other_version = entry.sha_mismatch;
    }
    else
    {
        auto &starter = starters[size_t(choice.mid(8).toInt())];
        std::vector<std::string> region_errors;
        Recipe recipe = starter.recipe;
        recipe.game = status.game;
        problems = CorruptionSession::get().loadRecipe(recipe);
        loaded = qs(recipe.name);
    }
    if (!problems.empty())
    {
        QStringList lines;
        for (auto &p : problems)
            lines << qs(p);
        showMessage(tr("This recipe doesn't fit this game:\n") + lines.join("\n"), true);
        return syncRecipeChoice();
    }
    showMessage(other_version ? tr("Loaded %1. It was made for another version of this ROM, so addresses may differ.").arg(loaded)
                : choice.startsWith("starter:") ? tr("Loaded the starter %1. Press Corrupt to try it; Save keeps your own copy.").arg(loaded)
                                                : tr("Loaded %1. Press Corrupt to start.").arg(loaded));
    syncRecipeChoice();
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
        QString why = reason == "load"             ? tr("when the game loaded")
                      : reason == "close"          ? tr("when the game closed")
                      : reason == "auto"           ? tr("while playing")
                      : reason == "before-restore" ? tr("before a restore")
                                                   : reason;
        auto item = new QListWidgetItem(QLocale().toString(when, QLocale::ShortFormat) + "  ·  " + why, backup_list);
        item->setData(Qt::UserRole, qs(b.path.string()));
    }
}

void CorruptionDock::chooseLibrary()
{
    auto dir = QFileDialog::getExistingDirectory(this, tr("Corruption library folder"),
                                                 qs(app.config->corruption_library_root));
    if (dir.isEmpty())
        return;
    app.config->corruption_library_root = dir.toStdString();
    CorruptionSession::get().setLibraryRoot(dir.toStdString());
    refreshRecipes();
    refreshBackups();
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

void CorruptionDock::restoreSelectedBackup()
{
    auto item = backup_list->currentItem();
    if (!item)
    {
        showMessage(tr("Pick a backup first"), true);
        return;
    }
    if (QMessageBox::question(this, tr("Restore this save"),
                              tr("Replace the game's save (your in-game progress) with the copy from %1, and restart "
                                 "the game? Your current save is copied first, so you can come back to it.")
                                  .arg(item->text())) != QMessageBox::Yes)
        return;
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
