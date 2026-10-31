#pragma once
#include <QDialog>

#include <utility>
#include <vector>

#include "corrupt/recipe_json.h"
#include "corrupt/rule.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QStackedWidget;

// "wram (work RAM)", "cgram (palette)": a target's name with what it holds.
QString targetLabel(corrupt::Target target);

// Edits one corruption rule: where (target and range, or a named region), what
// (the op and its settings), when (the trigger) and how much. Every change is
// validated against the game's memory sizes, and OK stays off until the rule is
// valid. It doesn't know the session, so tests can drive it without a game.
class RuleEditor : public QDialog
{
  public:
    RuleEditor(const corrupt::Rule &rule, const corrupt::Regions &regions,
               const corrupt::MemoryAccess &sizes, QWidget *parent = nullptr);

    corrupt::Rule rule() const; // the rule as the form stands
    QString error() const;      // why it isn't valid; empty when it is

  protected:
    // Fits again once the style has polished everything: the Mac's controls are taller.
    void showEvent(QShowEvent *event) override;

  private:
    struct Built
    {
        corrupt::Rule rule;
        QString error;
    };

    void buildForm();
    void load(const corrupt::Rule &rule);
    Built build() const;
    void refresh();
    void regionChosen();
    void fitToContents();

    corrupt::Rule original; // keeps what the form doesn't show (salt)
    corrupt::Regions regions;
    const corrupt::MemoryAccess &sizes;

    QLineEdit *name_edit;
    QComboBox *target_combo;
    QComboBox *region_combo;
    QLineEdit *first_edit;
    QLineEdit *last_edit;
    QComboBox *shape_combo;
    QSpinBox *rect_row, *rect_width, *rect_height;
    QWidget *rect_fields;

    QComboBox *op_combo;
    QStackedWidget *op_pages;
    QSpinBox *add_min, *add_max;
    QCheckBox *add_clamp;
    QSpinBox *random_min, *random_max;
    QComboBox *bitflip_mode;
    QSpinBox *bitflip_bits;
    QLineEdit *bitflip_mask;
    QComboBox *swap_mode;
    QLineEdit *swap_a, *swap_b;
    QComboBox *swap_width;
    QComboBox *copy_target;
    QLineEdit *copy_first, *copy_last;
    QSpinBox *copy_length;
    QComboBox *freeze_mode;
    QLineEdit *freeze_value;
    QSpinBox *shuffle_window;
    QSpinBox *shift_dx, *shift_dy;
    QCheckBox *shift_wrap;
    QLineEdit *shift_fill;
    QComboBox *flip_axis;
    QComboBox *levels_curve;
    QDoubleSpinBox *levels_amount;
    QComboBox *filter_kernel;
    QLineEdit *stamp_from;
    QLineEdit *fill_value;

    QComboBox *when_combo;
    QStackedWidget *when_pages;
    QSpinBox *once_frame, *every_frames, *hotkey;
    QDoubleSpinBox *chance;
    QCheckBox *window_enabled;
    QSpinBox *window_start, *window_end;

    QSpinBox *count_spin;
    QComboBox *pick_combo;
    QDoubleSpinBox *intensity_spin;
    QCheckBox *enabled_check;
    QCheckBox *avoid_check;

    class QScrollArea *scroll;
    QWidget *sections;
    QWidget *aim_box;
    class QFormLayout *where_form;
    QLabel *no_aim_hint;
    int shown_page = -1, shown_when_page = -1;
    QComboBox *role_combo;
    QComboBox *class_mode;
    std::vector<std::pair<corrupt::OpClass, QCheckBox *>> class_checks;

    QLabel *summary_label;
    QLabel *error_label;
    QDialogButtonBox *buttons;
};
