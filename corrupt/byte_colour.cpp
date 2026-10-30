#include "byte_colour.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace corrupt
{

namespace
{

constexpr double kStart = 0.5;      // the hue at black
constexpr double kRotations = -1.5; // turns of hue from black to white
constexpr double kSaturation = 1.0;

uint8_t channel(double v)
{
    return uint8_t(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
}

Rgb cubehelix(double fraction)
{
    double angle = 2.0 * M_PI * (kStart / 3.0 + 1.0 + kRotations * fraction);
    double amp = kSaturation * fraction * (1.0 - fraction) / 2.0;
    double c = std::cos(angle), s = std::sin(angle);
    return { channel(fraction + amp * (-0.14861 * c + 1.78277 * s)),
             channel(fraction + amp * (-0.29227 * c - 0.90649 * s)),
             channel(fraction + amp * (1.97294 * c)) };
}

} // namespace

Rgb byte_colour(uint8_t value)
{
    static const auto table = [] {
        std::array<Rgb, 256> t;
        for (int v = 0; v < 256; v++)
            t[size_t(v)] = cubehelix(v / 255.0);
        return t;
    }();
    return table[value];
}

Rgb byte_grey(uint8_t value)
{
    auto g = uint8_t(value * 100 / 255);
    return { g, g, g };
}

} // namespace corrupt
