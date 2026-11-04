#pragma once
#include <QWidget>

#include <functional>
#include <vector>

#include "CorruptionControl.hpp"
#include "WorkshopPage.hpp"

class ButtonsBar;
class IntensitySlider;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTimer;
class RecipeActions;
namespace ui
{
class StatusLine;
enum class Tone;
}

// The corruption panel while you play (#63): the recipe, Corrupt and Back to Normal, Strength for all rules
// and the dice, the recipe's buttons, its rules (on and strength), and one line each about
// the crash guard and the ROM. Everything else is in the Workshop, a link away.
class Cockpit : public QWidget
{
  public:
    Cockpit(CorruptionControl &control, RecipeActions &actions, QWidget *parent = nullptr);

    std::function<void(WorkshopPage page)> openWorkshop; // a link to one page
    std::function<void()> openWorkshopAgain;             // the Workshop button: where it was last
    std::function<void()> openGuide;
    void setToggleKey(const QString &key); // the Corrupt/Stop shortcut's name ("F7"), or empty
    void setKeepKey(const QString &key);   // Keep This Moment's (#47), or empty
    void refresh();                        // runs four times a second while shown
    void refreshRecipes();                 // the recipe list, read again
    void showMessage(const QString &text, bool error = false);
    void setMessageFade(int ms) { fade_ms = ms; } // how long a message stays (12 s; tests use less)

  protected:
    void changeEvent(QEvent *event) override;

  private:
    void rebuildRules(const PanelStatus &status);
    void showSeed(const QString &seed);
    void syncRecipeChoice(const PanelStatus &status);
    void recipeChosen(int index);
    void fitRulesTable();
    void showState(const QString &state, const QString &text);
    void setLink(QLabel *label, QLabel *icon, const char *icon_name, const QString &page, const QString &text,
                 const QString &where, ui::Tone tone);
    void startFade();

    CorruptionControl &control;
    RecipeActions &actions;
    QTimer *timer;
    QString toggle_key, keep_key;

    QLabel *game_label;
    QLabel *state_label;
    QString shown_state;
    QLabel *moment_note;
    QLabel *start_card;
    QComboBox *recipe_combo;
    QPushButton *save_button;
    ui::StatusLine *message_line;
    QTimer *fade_timer;
    int fade_ms = 12000;
    bool showing_error = false;
    QPushButton *corrupt_button;
    QPushButton *back_button;
    QPushButton *here_button; // Normal, Stay Here (#70)
    QPushButton *keep_button; // Keep This Moment, or Stop Playing while one plays (#47)
    QString shown_keep;
    IntensitySlider *chaos_slider;
    QLineEdit *seed_edit;
    QPushButton *reroll_button;
    QWidget *run_controls;
    QPushButton *mark_button;
    QWidget *buttons_box;
    ButtonsBar *buttons_bar;
    QWidget *rules_box;
    QTableWidget *rules_table;
    std::vector<IntensitySlider *> rule_sliders;
    double all_rules = 1.0; // Strength, all rules, for the rule sliders' tooltips
    QLabel *rules_empty;
    QLabel *guard_label;
    QLabel *guard_icon;
    QLabel *rom_label;
    QLabel *rom_icon;

    std::string shown_game = "-";
    std::string shown_message;
    uint64_t shown_generation = UINT64_MAX;
    uint64_t shown_rules_generation = UINT64_MAX;
    uint64_t shown_library_generation = 0;
    uint64_t shown_choice_generation = UINT64_MAX;
    std::filesystem::path shown_recipe_path;
    int shown_blank = -1;
    int shown_active = -1;

    enum { kOn, kName, kStrength, kColumns };
};
