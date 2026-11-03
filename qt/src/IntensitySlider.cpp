#include "IntensitySlider.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>

#include <algorithm>
#include <cmath>

IntensitySlider::IntensitySlider(QWidget *parent)
    : QWidget(parent)
{
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 0, 4, 0);
    slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(0, 100);
    slider->setPageStep(10);
    label = new QLabel(this);
    label->setMinimumWidth(label->fontMetrics().horizontalAdvance("100%"));
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(slider, 1);
    layout->addWidget(label);

    connect(slider, &QSlider::valueChanged, this, [this](int v) {
        value = v / 100.0;
        showValue();
        if (onChanged)
            onChanged(value);
    });
    setIntensity(1.0);
}

void IntensitySlider::setIntensity(double intensity)
{
    value = std::clamp(intensity, 0.0, 1.0);
    QSignalBlocker block(slider);
    slider->setValue(int(std::lround(value * 100)));
    showValue();
}

void IntensitySlider::refresh(double intensity)
{
    if (!slider->isSliderDown() && std::abs(intensity - value) > 1e-9)
        setIntensity(intensity);
}

void IntensitySlider::showValue()
{
    label->setText(QString("%1%").arg(std::lround(value * 100)));
    redescribe();
}

void IntensitySlider::redescribe()
{
    if (!describe)
        return;
    auto text = describe(value);
    setToolTip(text);
    slider->setToolTip(text);
    slider->setAccessibleDescription(text);
}
