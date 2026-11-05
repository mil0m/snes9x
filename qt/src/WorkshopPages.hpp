#pragma once
#include <QWidget>

#include <array>
#include <functional>
#include <optional>
#include <vector>

#include "CorruptionControl.hpp"
#include "WorkshopPage.hpp"

class IntensitySlider;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class ExploreCharts;
class ExploreRunner;
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

// What the Workshop's sidebar says beside each page's name (#65): a few words, empty when
// there's nothing to say; a warning (a crash, the guard off) in the warning colour.
struct SidebarNote
{
    QString text;
    bool warning = false;
};
// `others`: people other than you with files for this game in the library.
// The recipe the Workshop is changing, in a few words for the top of its sidebar (#74):
// "Not saved", "Changed since saved", "Saved", "Empty · not saved", "Starter · not yours yet".
QString recipe_state(const PanelStatus &status);

// The Safety page's lines under each switch (#76): what it does, what it costs, what off
// means, whether it has anything to do for this recipe now, and what it has done so far.
QString guard_note(const PanelStatus &status);
QString ahead_note(const PanelStatus &status);
QString instant_note(const PanelStatus &status);
QString policy_note(corrupt::PolicyKind policy);

std::array<SidebarNote, 8> sidebar_notes(const PanelStatus &status, size_t others, bool remote_on);

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
    double all_rules = 1.0; // Strength, all rules, for the rule sliders' tooltips
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
    QLabel *detect_note, *test_first_note, *instant_note_label, *policy_note_label;
    QLabel *crash_label;
    QListWidget *backup_list;
    QLabel *backups_empty;
    QPushButton *restore_backup_button;
    ui::StatusLine *message;
    int ticks = 0;
};

// Kept moments (#47): this game's, everyone's in the library, newest first. Play one back
// (corruption off, any button takes over), rename, delete, or show its file to share.
class MomentsPage : public QWidget
{
  public:
    // With a runner, Explore starts the explorer (#45) on the selected moment.
    MomentsPage(CorruptionControl &control, ExploreRunner *runner = nullptr, QWidget *parent = nullptr);
    void refresh(const PanelStatus &status);
    void reload(); // the list, read from the library again
    std::function<bool(const QString &title, const QString &question)> confirm;

  private:
    std::optional<corrupt::MomentInfo> selected() const;
    void showSelected();

    CorruptionControl &control;
    std::vector<corrupt::MomentInfo> list;
    QTableWidget *table;
    QLabel *empty_label;
    QLabel *details;
    QWidget *actions_row;
    QPushButton *play_button, *rename_button, *delete_button, *folder_button, *explore_button = nullptr;
    ui::StatusLine *message;
    ExploreRunner *runner;
    PanelStatus last;
    int ticks = 0;
    uint64_t shown_keeps = UINT64_MAX;
    std::string shown_game;
    bool filling = false;
    bool renaming = false;
};

// Explored (#46): what the explorer found from a kept moment, best first, with the picture
// strip of each; Play plays a candidate's run (its kept moment), Keep as Recipe saves it.
class ExploredPage : public QWidget
{
  public:
    ExploredPage(CorruptionControl &control, ExploreRunner &runner, QWidget *parent = nullptr);
    void refresh(const PanelStatus &status);
    void reload(); // the explorations, read from the library again
    std::function<bool(const QString &title, const QString &question)> confirm;

  private:
    const corrupt::Library::ExplorationEntry *exploration() const;
    const corrupt::Candidate *selected() const;
    void showExploration();
    void showSelected();
    void showRunning();

    CorruptionControl &control;
    ExploreRunner &runner;
    std::vector<corrupt::Library::ExplorationEntry> list;
    QWidget *running_row;
    QLabel *running_label;
    QPushButton *stop_button;
    QComboBox *chooser;
    QLabel *summary;
    QTableWidget *table;
    QLabel *strip;
    QLabel *empty_label;
    QLabel *details;
    QWidget *actions_row;
    QPushButton *play_button, *keep_button, *gallery_button, *delete_button, *folder_button;
    ui::StatusLine *message;
    QWidget *charts_heading;
    ExploreCharts *charts; // #80
    int ticks = 0;
    unsigned shown_tried = UINT_MAX;
    std::string shown_game;
    bool filling = false;
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
