#pragma once
#include <QDockWidget>
#include <QTimer>

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

// The corruption panel: pick a recipe for the loaded game, toggle it, re-roll,
// save it to the library, and restore save backups.
class CorruptionDock : public QDockWidget
{
    Q_OBJECT

  public:
    CorruptionDock(EmuApplication &app, QWidget *parent);

    void refreshRecipes();
    void refreshBackups();

  private:
    void refreshStatus();
    void rebuildRules();
    void chooseLibrary();
    void loadSelectedRecipe();
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
    void addExample();
    void restoreSelectedBackup();
    void showMessage(const QString &text, bool error = false);

    EmuApplication &app;
    QTimer timer;
    std::vector<corrupt::RecipeEntry> entries;
    uint64_t shown_generation = UINT64_MAX;
    uint64_t shown_rules_generation = UINT64_MAX;
    std::string shown_game;
    std::string shown_message;
    int ticks = 0;
    std::vector<unsigned> shown_rule_crashes;

    QLabel *game_label;
    QLineEdit *library_edit;
    QComboBox *recipe_combo;
    QComboBox *example_combo;
    QCheckBox *moments_check;
    QCheckBox *active_check;
    QLineEdit *seed_edit;
    QTableWidget *rules_table;
    std::vector<IntensitySlider *> rule_sliders;
    IntensitySlider *chaos_slider;
    QPushButton *save_button;
    QPushButton *add_rule_button;
    QPushButton *edit_rule_button;
    QPushButton *duplicate_rule_button;
    QPushButton *remove_rule_button;
    QCheckBox *detect_check;
    QComboBox *policy_combo;
    QLabel *crash_label;
    QLabel *status_label;
    QListWidget *backup_list;
    QLabel *message_label;
};
