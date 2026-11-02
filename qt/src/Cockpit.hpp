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
}

// The corruption panel while you play (#63): the recipe, Corrupt and Back to Normal, Chaos
// and the dice, the recipe's buttons, its rules (on and strength), and one line each about
// the crash guard and the ROM. Everything else is in the Workshop, a link away.
class Cockpit : public QWidget
{
  public:
    Cockpit(CorruptionControl &control, RecipeActions &actions, QWidget *parent = nullptr);

    std::function<void(WorkshopPage page)> openWorkshop;
    std::function<void()> openGuide;
    void setToggleKey(const QString &key); // the Corrupt/Stop shortcut's name ("F7"), or empty
    void refresh();                        // runs four times a second while shown
    void refreshRecipes();                 // the recipe list, read again
    void showMessage(const QString &text, bool error = false);

  private:
    void rebuildRules(const PanelStatus &status);
    void syncRecipeChoice(const PanelStatus &status);
    void recipeChosen(int index);
    void fitRulesTable();

    CorruptionControl &control;
    RecipeActions &actions;
    QTimer *timer;
    QString toggle_key;

    QLabel *game_label;
    QLabel *start_card;
    QComboBox *recipe_combo;
    QPushButton *save_button;
    ui::StatusLine *message_line;
    bool showing_error = false;
    QPushButton *corrupt_button;
    QPushButton *back_button;
    QLabel *run_label;
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
    QLabel *rules_empty;
    QLabel *guard_label;
    QLabel *rom_label;

    std::string shown_game = "-";
    std::string shown_message;
    uint64_t shown_generation = UINT64_MAX;
    uint64_t shown_rules_generation = UINT64_MAX;
    uint64_t shown_library_generation = 0;
    uint64_t shown_choice_generation = UINT64_MAX;
    std::filesystem::path shown_recipe_path;
    int shown_active = -1;

    enum { kOn, kName, kStrength, kColumns };
};
