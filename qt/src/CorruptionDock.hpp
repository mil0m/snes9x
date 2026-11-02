#pragma once
#include <QDockWidget>
#include <QTimer>

#include <memory>

#include "MemoryViewer.hpp"
#include "Starters.hpp"
#include "FunctionsDialog.hpp"
#include "SurvivorsDialog.hpp"
#include "corrupt/library.h"

class EmuApplication;
class IntensitySlider;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QToolButton;
namespace ui
{
class ElidedLabel;
class StatusLine;
} // namespace ui

// The corruption panel: pick a recipe for the loaded game, toggle it, re-roll,
// save it to the library, and restore save backups.
class CorruptionDock : public QDockWidget
{
    Q_OBJECT

  public:
    CorruptionDock(EmuApplication &app, QWidget *parent);

    void refreshRecipes();
    void refreshBackups();
    void openGuide(const QString &section); // a "## heading" of the guide, or empty for the top

  private:
    void refreshStatus();
    void rebuildRules();
    void chooseLibrary();
    void exportRom();
    void saveAs();
    void save();
    void newRecipe();
    bool confirmDiscard(const QString &action); // true when there are no edits to lose, or the user agrees
    int selectedRule();
    void selectRule(int row);
    void addRule();
    void editRule(int row);
    void duplicateRule();
    void removeRule();
    void recipeChosen(int index); // a library recipe or a starter, from the recipe list
    void syncRecipeChoice();      // the list shows what's loaded
    QString hotkey(int shortcut) const; // "F7", or "no hotkey"
    static QString policyText(corrupt::PolicyKind p);
    void restoreSelectedBackup();
    void showMessage(const QString &text, bool error = false);
    void openViewer();
    static QTableWidgetItem *crashItem(unsigned crashes);
    void fitRulesTable();
    // SNES9X_UI_TOUR=<dir>: screenshots of every corruption window, for UX work (see
    // docs/corruption/README.md, "Developing").
    void tour(const QString &dir);

    EmuApplication &app;
    QTimer timer;
    std::vector<corrupt::RecipeEntry> entries;
    uint64_t shown_generation = UINT64_MAX;
    uint64_t shown_library_generation = 0;
    uint64_t shown_rules_generation = UINT64_MAX;
    std::string shown_game;
    std::string shown_message;
    int ticks = 0;
    std::vector<unsigned> shown_rule_crashes;
    uint64_t shown_choice_generation = UINT64_MAX;
    std::filesystem::path shown_recipe_path;

    std::vector<Starter> starters; // the example recipes that come with the app

    QLabel *game_label;
    QPushButton *guide_button;
    QLabel *start_card;
    ui::ElidedLabel *library_label;
    QComboBox *recipe_combo;
    QCheckBox *moments_check;
    MemoryViewer *memory_viewer = nullptr;
    std::unique_ptr<MemorySource> memory_source;
    std::unique_ptr<SurvivorSource> survivor_source;
    SurvivorsDialog *survivors_dialog = nullptr;
    std::unique_ptr<FunctionSource> function_source;
    FunctionsDialog *functions_dialog = nullptr;
    QPushButton *functions_button = nullptr;
    class GuideWindow *guide = nullptr;
    QPushButton *survivors_button = nullptr;
    QPushButton *corrupt_button;
    QPushButton *back_button;
    QLabel *rules_empty;
    QLineEdit *seed_edit;
    QPushButton *reroll_button;
    class ButtonsBar *buttons_bar;
    QTableWidget *rules_table;
    std::vector<IntensitySlider *> rule_sliders;
    IntensitySlider *chaos_slider;
    QPushButton *save_button;
    QPushButton *add_rule_button;
    QPushButton *edit_rule_button;
    QPushButton *duplicate_rule_button;
    QPushButton *remove_rule_button;
    QCheckBox *detect_check;
    QCheckBox *test_first_check;
    // Remote control from a browser (#53).
    class RemoteControl *remote = nullptr;
    QCheckBox *remote_check = nullptr;
    QLabel *remote_label = nullptr;
    QLineEdit *remote_address = nullptr;
    void setRemote(bool on);
    void refreshRemote();
    QString remoteLink() const;
    QComboBox *policy_combo;
    QLabel *crash_label;
    QLabel *status_label;
    QLabel *rom_label;
    QPushButton *restore_rom_button;
    QPushButton *clear_paint_button;
    QListWidget *backup_list;
    ui::StatusLine *message_line;
    bool showing_error = false;
    int shown_active = -1;

    // The rules table's columns.
    enum { kOn, kName, kWhat, kStrength, kCrashes, kColumns };
};
