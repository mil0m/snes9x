#include "doctest/doctest.h"
#include "IntensitySlider.hpp"

#include <QLabel>
#include <QSlider>

TEST_CASE("the slider shows an intensity as a percentage")
{
    IntensitySlider s;
    s.setIntensity(0.5);
    CHECK(s.intensity() == doctest::Approx(0.5));
    CHECK(s.findChild<QLabel *>()->text() == "50%");
    s.setIntensity(0.125); // a dampened rule: shown rounded, kept exact until moved
    CHECK(s.findChild<QLabel *>()->text() == "13%");
    CHECK(s.intensity() == doctest::Approx(0.125));
}

TEST_CASE("setting it from code is silent; moving it reports the new intensity")
{
    IntensitySlider s;
    std::vector<double> got;
    s.onChanged = [&](double v) { got.push_back(v); };
    s.setIntensity(0.3);
    CHECK(got.empty());
    s.findChild<QSlider *>()->setValue(70);
    REQUIRE(got.size() == 1);
    CHECK(got[0] == doctest::Approx(0.7));
    CHECK(s.findChild<QLabel *>()->text() == "70%");
    CHECK(s.intensity() == doctest::Approx(0.7));
}

TEST_CASE("values outside 0..1 are clamped")
{
    IntensitySlider s;
    s.setIntensity(3.0);
    CHECK(s.intensity() == 1.0);
    s.setIntensity(-1.0);
    CHECK(s.intensity() == 0.0);
    CHECK(s.findChild<QLabel *>()->text() == "0%");
}

TEST_CASE("while the user drags it, refreshes from the engine don't move it")
{
    IntensitySlider s;
    s.setIntensity(0.4);
    s.findChild<QSlider *>()->setSliderDown(true);
    s.refresh(0.9);
    CHECK(s.intensity() == doctest::Approx(0.4));
    s.findChild<QSlider *>()->setSliderDown(false);
    s.refresh(0.9);
    CHECK(s.intensity() == doctest::Approx(0.9));
}
