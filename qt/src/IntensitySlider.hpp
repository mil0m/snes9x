#pragma once
#include <QWidget>

#include <functional>

class QLabel;
class QSlider;

// A 0-100 % slider with its value beside it, for rule and recipe intensity.
// Setting it from code is silent; moving it calls onChanged with 0..1.
class IntensitySlider : public QWidget
{
  public:
    explicit IntensitySlider(QWidget *parent = nullptr);

    void setIntensity(double intensity);
    double intensity() const { return value; }

    // Follows the engine (a crash guard dampen, a hotkey), unless the user is dragging.
    void refresh(double intensity);

    std::function<void(double)> onChanged;

  private:
    void showValue();

    QSlider *slider;
    QLabel *label;
    double value = 1.0; // exact, so a dampened 12.5 % isn't rounded until it's moved
};
