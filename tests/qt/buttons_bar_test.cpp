#include "doctest/doctest.h"
#include "ButtonsBar.hpp"

#include <QPushButton>

using namespace corrupt;

TEST_SUITE("buttons bar")
{
    TEST_CASE("hidden with no buttons; one push button per slot, named and in order")
    {
        ButtonsBar bar;
        bar.setButtons({});
        CHECK(bar.isHidden());
        bar.setButtons({ { 1, "gravity off", "gravity off", true, true }, { 4, "Burst + 1 more", "Burst\nSparkle", false, false } });
        CHECK_FALSE(bar.isHidden());
        auto one = bar.findChild<QPushButton *>("button_1");
        auto four = bar.findChild<QPushButton *>("button_4");
        REQUIRE(one);
        REQUIRE(four);
        CHECK(one->text() == "gravity off");
        CHECK(one->isCheckable());
        CHECK(one->isChecked());
        CHECK(four->text() == "Burst + 1 more");
        CHECK_FALSE(four->isCheckable());
        CHECK(four->toolTip().contains("Sparkle"));
    }

    TEST_CASE("a press says which slot; a toggle waits for the game to say it's on")
    {
        ButtonsBar bar;
        std::vector<int> pressed;
        bar.onPress = [&](int key) { pressed.push_back(key); };
        bar.setButtons({ { 2, "jump +3", "jump +3", true, false } });
        auto push = bar.findChild<QPushButton *>("button_2");
        REQUIRE(push);
        push->click();
        CHECK(pressed == std::vector<int>{ 2 });
        CHECK_FALSE(push->isChecked()); // not on until the refresh says so
        bar.setButtons({ { 2, "jump +3", "jump +3", true, true } });
        CHECK(bar.findChild<QPushButton *>("button_2") == push); // same button: nothing moved
        CHECK(push->isChecked());
    }
}
