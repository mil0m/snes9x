#pragma once
#include <QDialog>

#include "FunctionSource.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTimer;
namespace ui
{
class StatusLine;
}

// Change a function (#60, with #35): its code line by line, and for the selected line what it
// can become: another number, the opposite branch (or always, or never), the other half of a
// math pair, or a call skipped or sent to another function. Apply changes it at the next
// frame, Put Back undoes it, Keep in Recipe keeps it (on a hotkey, starting on or not).
class FunctionEditor : public QDialog
{
  public:
    FunctionEditor(FunctionSource &source, const FunctionRow &function, QWidget *parent = nullptr);
    void refresh(); // the options again, and each line's state now

  protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

  private:
    void update(); // each line's state now
    void selectionChanged();
    int selectedRow() const;     // the selected line's option, or -1
    int optionAt(int row) const; // the option a table row shows (rows can be sorted), or -1
    // The change the editor describes for the selected line; nullopt (with why) if none.
    std::optional<corrupt::Change> wanted(QString &why) const;
    void apply();
    void putBack();
    void keep();
    void forget();
    void say(const QString &text, int tone = 0);
    const corrupt::Change *kept(uint32_t at) const;

    FunctionSource &source;
    FunctionRow function;
    std::vector<corrupt::ChangeOption> options;
    std::vector<corrupt::FeedReading> feeds; // what each option's value goes into (#77)
    std::vector<FunctionRow> callable; // functions a call may go to, busiest first
    QTimer *timer;
    ui::StatusLine *not_running;
    QLabel *function_note; // the busiest functions break more easily (#77)
    QLabel *empty_label;
    QTableWidget *table;
    QWidget *editor;
    QStackedWidget *pages;
    QSpinBox *number;
    QCheckBox *swap_math; // a number's instruction can also swap (ADC # -> SBC #)
    QComboBox *branch;
    QLabel *risk_note;
    QLabel *math_label;
    QComboBox *call;
    QComboBox *hotkey;
    QCheckBox *start_on;
    QCheckBox *timer_check;
    QDoubleSpinBox *timer_for;
    QDoubleSpinBox *timer_every;
    QPushButton *apply_button;
    QPushButton *put_back_button;
    QPushButton *keep_button;
    QPushButton *forget_button;
    ui::StatusLine *message;
};
