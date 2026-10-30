#include "doctest/doctest.h"
#include "corrupt/byte_colour.h"

#include <cstdlib>
#include <set>

using namespace corrupt;

namespace
{
int brightness(Rgb c)
{
    return 299 * c.r + 587 * c.g + 114 * c.b; // perceived, times 1000
}

int distance(Rgb a, Rgb b)
{
    return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b);
}
} // namespace

TEST_CASE("zero is black and 0xFF is white")
{
    CHECK(byte_colour(0x00) == Rgb{ 0, 0, 0 });
    CHECK(byte_colour(0xFF) == Rgb{ 255, 255, 255 });
}

TEST_CASE("every value gets its own colour")
{
    std::set<uint32_t> seen;
    for (int v = 0; v < 256; v++)
    {
        Rgb c = byte_colour(uint8_t(v));
        seen.insert(uint32_t(c.r) << 16 | uint32_t(c.g) << 8 | c.b);
    }
    CHECK(seen.size() == 256);
}

TEST_CASE("brightness rises with the value, and neighbours look alike")
{
    for (int v = 0; v + 4 < 256; v++)
        CHECK(brightness(byte_colour(uint8_t(v))) < brightness(byte_colour(uint8_t(v + 4))));
    for (int v = 0; v < 255; v++)
        CHECK(distance(byte_colour(uint8_t(v)), byte_colour(uint8_t(v + 1))) < 40);
    // Far-apart values are easy to tell apart.
    CHECK(distance(byte_colour(0x20), byte_colour(0x60)) > 120);
    CHECK(distance(byte_colour(0x60), byte_colour(0xA0)) > 120);
}

TEST_CASE("the grey ramp is dim, grey, and rising")
{
    CHECK(byte_grey(0x00) == Rgb{ 0, 0, 0 });
    for (int v = 0; v < 255; v++)
    {
        Rgb a = byte_grey(uint8_t(v)), b = byte_grey(uint8_t(v + 1));
        CHECK(a.r == a.g);
        CHECK(a.g == a.b);
        CHECK(a.r <= b.r);
    }
    CHECK(byte_grey(0xFF).r <= 120); // dim, so an overlay painted on top stands out
    CHECK(byte_grey(0xFF).r >= 90);
}
