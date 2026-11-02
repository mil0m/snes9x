#pragma once
#include <QDialog>

#include "FunctionSource.hpp"

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QTimer;
namespace ui
{
class StatusLine;
}

// Kill switches (#34) and what came after (#58, #59): the functions the game runs, busiest now
// first. Tick one off and the game skips it; name it for everyone using the library; protect
// it from other corruption; put it on a hotkey and keep it in the recipe; see its code in the
// ROM. While open it updates twice a second in place (rows keep their order until Refresh, so
// nothing moves under the mouse).
class FunctionsDialog : public QDialog
{
  public:
    explicit FunctionsDialog(FunctionSource &source, QWidget *parent = nullptr);
    void refresh(); // reload, busiest now first

    // Show in ROM: the memory viewer on the ROM, the Functions overlay, these bytes selected.
    std::function<void(uint32_t first, uint32_t last)> onShowInRom;

  protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

  private:
    void update(); // the same rows, their numbers and states now
    void fill(int row, const FunctionRow &r);
    void offChanged(QTableWidgetItem *item);
    void selectionChanged();
    void applyFilter();
    void keepSelected();
    void forgetSelected();
    void nameSelected();
    void protectChanged();
    void showSelected();
    void changeSelected();
    int selectedRow() const;
    void say(const QString &text, int tone = 0); // ui::Tone as int, to keep UiKit out of here

    FunctionSource &source;
    std::vector<FunctionRow> shown;
    bool filling = false;
    bool selecting = false; // the controls are being set from the selected row
    QTimer *timer;
    QLabel *empty_label;
    ui::StatusLine *not_running;
    QLineEdit *filter;
    QTableWidget *table;
    QWidget *details;
    QLabel *selected_label;
    QLineEdit *name;
    QCheckBox *protect;
    QCheckBox *deep;
    QComboBox *hotkey;
    QCheckBox *start_off;
    QCheckBox *timer_check;
    QDoubleSpinBox *timer_for;
    QDoubleSpinBox *timer_every;
    QPushButton *keep_button;
    QPushButton *forget_button;
    QPushButton *show_button;
    QPushButton *change_button;
    ui::StatusLine *message;
};
