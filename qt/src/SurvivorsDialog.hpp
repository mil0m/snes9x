#pragma once
#include <QDialog>

#include "corrupt/survivors.h"
#include "corrupt/patches.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
namespace ui
{
class StatusLine;
}

// What the dialog needs from the running game.
struct SurvivorSource
{
    virtual std::vector<corrupt::Survivor> survivors() = 0;
    virtual std::vector<corrupt::Pin> pins() = 0;
    virtual std::string pin(const std::vector<uint32_t> &offsets, const std::string &note) = 0; // error or empty
    virtual void unpin(const std::vector<uint32_t> &offsets) = 0;
    virtual ~SurvivorSource() = default;
};

// Survivors (ROM changes the game used often, with no crash after) and the recipe's pins:
// keep the good ones, let go of others.
class SurvivorsDialog : public QDialog
{
  public:
    explicit SurvivorsDialog(SurvivorSource &source, QWidget *parent = nullptr);
    void refresh();

  private:
    void pinSelected();
    void unpinSelected();
    void selectionChanged();

    SurvivorSource &source;
    std::vector<corrupt::Survivor> shown;
    std::vector<corrupt::Pin> shown_pins;
    QLabel *empty_label;
    ui::StatusLine *message;
    QLabel *pins_empty;
    QWidget *pin_row;
    QTableWidget *table;
    QLineEdit *note_edit;
    QPushButton *pin_button;
    QTableWidget *pins_table;
    QPushButton *unpin_button;
};
