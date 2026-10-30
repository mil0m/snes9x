#pragma once
#include <cstdint>

namespace corrupt
{

struct Rgb
{
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb &) const = default;
};

// The pixel view's colour for a byte: 0x00 black, 0xFF white, and in between a hue that
// turns while the brightness rises steadily (Green's "cubehelix"), so fills, gradients and
// repeating tables show as bands, close values look alike and far ones don't.
Rgb byte_colour(uint8_t value);

// A dim grey ramp for the same byte, for when an overlay is painted on top.
Rgb byte_grey(uint8_t value);

} // namespace corrupt
