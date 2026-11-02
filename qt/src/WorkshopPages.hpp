#pragma once
#include <QWidget>

#include <functional>
#include <vector>

#include "CorruptionControl.hpp"

class IntensitySlider;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class RecipeActions;
class SurvivorsPanel;
struct SurvivorSource;
namespace ui
{
class ElidedLabel;
class StatusLine;
} // namespace ui

// The Workshop's own pages (#63); Functions and Memory are FunctionsPage and MemoryViewer.
// Each shows a PanelStatus (refresh) and acts through CorruptionControl, so tests drive them
// with a fake.

// The recipe: its rules in full, everything else it keeps, and saving and sharing it.
class RecipePage : public QWidget
{
  public:
    RecipePage(CorruptionControl &control, RecipeActions &actions, QWidget *parent = nullptr);
    void refresh(const PanelStatus &status);
    void say(const QString &text, bool error = false);

  private:
    void rebuildRules(const PanelStatus &status);
    void rebuildKept(const PanelStatus &status);
    void fitTables();
    int selectedRule() const;

    CorruptionControl &control;
    RecipeActions &actions;
    QLabel *name_label;
    QLabel *notes_label;
    QCheckBox *moments_check;
    ui::StatusLine *message;
    QPushButton *save_button;
    QTableWidget *rules_table;
    std::vector<IntensitySlider *> rule_sliders;
    QLabel *rules_empty;
    QPushButton *add_button;
    QPushButton *edit_button;
    QPushButton *duplicate_button;
    QPushButton *remove_button;
    QTableWidget *kept_table;
    QLabel *kept_empty;
    QPushButton *remove_kept_button;
    std::vector<corrupt::KeptItem> kept;
    uint64_t shown_generation = UINT64_MAX;
    uint64_t shown_rules_generation = UINT64_MAX;
    std::vector<unsigned> shown_crashes;
    bool has_recipe = false;

    enum { kOn, kName, kWhat, kStrength, kCrashes, kColumns };
};

// The ROM: what changed, what the game taught the map, survivors and pins, and the actions
// on the ROM as a whole.
class RomPage : public QWidget
{
  public:
    RomPage(CorruptionControl &control, SurvivorSource &survivors, QWidget *parent = nullptr);
    void refresh(const PanelStatus &status);
    SurvivorsPanel *survivors() const { return survivors_panel; }
    std::function<QString(const QString &title, const QString &start)> askSavePath; // empty: cancelled

  private:
    CorruptionControl &control;
    QLabel *changed_label;
    QLabel *learned_label;
    QPushButton *restore_button;
    QPushButton *clear_paint_button;
    ui::StatusLine *message;
    SurvivorsPanel *survivors_panel;
    int ticks = 0;
};

// Safety: the crash guard's switches, what happened, and the game's save backups.
class SafetyPage : public QWidget
{
  public:
    SafetyPage(CorruptionControl &control, QWidget *parent = nullptr);
    void refresh(const PanelStatus &status);
    void refreshBackups();
    std::function<bool(const QString &title, const QString &question)> confirm;

  private:
    CorruptionControl &control;
    QCheckBox *detect_check;
    QCheckBox *test_first_check;
    QCheckBox *instant_check;
    QComboBox *policy_combo;
    QLabel *crash_label;
    QListWidget *backup_list;
    QLabel *backups_empty;
    QPushButton *restore_backup_button;
    ui::StatusLine *message;
    int ticks = 0;
};

// Sharing: the library folder and who's in it, and remote control from a browser.
class SharingPage : public QWidget
{
  public:
    SharingPage(CorruptionControl &control, QWidget *parent = nullptr);
    void refresh(const PanelStatus &status);
    std::function<QString(const QString &start)> askFolder; // empty: cancelled
    std::function<void()> libraryChanged;                    // the recipe lists read it again
    std::function<void()> lookForRecipes;

  private:
    void refreshPlayers();

    CorruptionControl &control;
    ui::ElidedLabel *library_label;
    QListWidget *players_list;
    ui::StatusLine *conflicts_line;
    QCheckBox *remote_check;
    QLabel *remote_label;
    QLineEdit *remote_address;
    QWidget *remote_details;
    int ticks = 0;
};
