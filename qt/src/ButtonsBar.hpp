#pragma once
#include <QWidget>

#include <functional>
#include <vector>

#include "corrupt/buttons.h"

class QGridLayout;
class QPushButton;

// The recipe's buttons (#63): one push button per slot in use, named after what it does.
// A toggle (only switches and changes on it) shows whether it's on. Two to a row, so they
// fit the panel. Hidden while there are none.
class ButtonsBar : public QWidget
{
  public:
    explicit ButtonsBar(QWidget *parent = nullptr);
    void setButtons(const std::vector<corrupt::ButtonSlot> &buttons);
    std::function<void(int key)> onPress;

  private:
    QGridLayout *grid;
    std::vector<corrupt::ButtonSlot> shown;
    std::vector<QPushButton *> pushes;
};
