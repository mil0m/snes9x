#include "doctest/doctest.h"
#include "corrupt/code_map.h"
#include "corrupt/rom_heat.h"

using namespace corrupt;
namespace cf = codeflag;

TEST_SUITE("ROM live heat")
{
    TEST_CASE("a touched byte glows, loses its touched bit, and fades out over the fade")
    {
        std::vector<uint8_t> flags(64, 0);
        RomHeat heat;
        heat.reset(flags.size());
        heat.observe(flags.data(), flags.size()); // the first scan after starting only clears
        flags[10] = cf::Opcode | cf::Touched;
        flags[40] = cf::Data | cf::Touched;
        flags[41] = cf::Data; // known, but not used this frame
        heat.observe(flags.data(), flags.size());
        CHECK(heat.level(10) == 255);
        CHECK(heat.level(40) == 255);
        CHECK(heat.level(41) == 0);
        CHECK(flags[10] == cf::Opcode); // the bit is cleared, the rest of the map kept
        CHECK(flags[40] == cf::Data);
        for (uint32_t i = 0; i < RomHeat::kFadeFrames / 2; i++)
            heat.observe(flags.data(), flags.size());
        CHECK(heat.level(10) > 100);
        CHECK(heat.level(10) < 155);
        flags[40] |= cf::Touched; // used again: bright again
        heat.observe(flags.data(), flags.size());
        CHECK(heat.level(40) == 255);
        for (uint32_t i = 0; i < RomHeat::kFadeFrames; i++)
            heat.observe(flags.data(), flags.size());
        CHECK(heat.level(10) == 0);
        auto levels = heat.levels();
        REQUIRE(levels.size() == flags.size());
        CHECK(levels[10] == 0);
    }

    TEST_CASE("bits set while nobody watched don't light up everything at once")
    {
        std::vector<uint8_t> flags(1000, 0);
        RomHeat heat;
        heat.reset(flags.size());
        for (size_t i = 0; i < flags.size(); i += 3)
            flags[i] = cf::Opcode | cf::Touched; // all the play before the viewer opened
        heat.observe(flags.data(), flags.size());
        for (size_t i = 0; i < flags.size(); i++)
            CHECK(heat.level(i) == 0);
        heat.pause(); // the viewer went to another memory
        flags[7] |= cf::Touched;
        heat.observe(flags.data(), flags.size());
        CHECK(heat.level(7) == 0);
        flags[7] |= cf::Touched;
        heat.observe(flags.data(), flags.size());
        CHECK(heat.level(7) == 255);
    }

    TEST_CASE("changed bytes the crash blame saw used glow too (it clears their bits first)")
    {
        std::vector<uint8_t> flags(64, 0);
        RomHeat heat;
        heat.reset(flags.size());
        heat.observe(flags.data(), flags.size());
        std::vector<uint32_t> blamed{ 20, 999 }; // out of range is ignored
        heat.observe(flags.data(), flags.size(), blamed);
        CHECK(heat.level(20) == 255);
    }

    TEST_CASE("sizes that aren't a multiple of 8, and a map of another size, are handled")
    {
        std::vector<uint8_t> flags(13, 0);
        RomHeat heat;
        heat.reset(13);
        heat.observe(flags.data(), flags.size());
        flags[12] = cf::Touched;
        heat.observe(flags.data(), flags.size());
        CHECK(heat.level(12) == 255);
        CHECK(heat.level(500) == 0);
        std::vector<uint8_t> other(5, cf::Touched);
        heat.observe(other.data(), other.size()); // a different map: starts over, no crash
        CHECK(heat.levels().size() == 5);
    }
}
