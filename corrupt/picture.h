#pragma once
#include <cstdint>
#include <vector>

// Pictures of the game's screen, for the explorer (#45): a thumbnail of every frame it plays,
// and the full picture for the strips it keeps. RGB, 8 bits a channel, rows top down.
namespace corrupt
{

struct Picture
{
    uint16_t w = 0, h = 0;
    std::vector<uint8_t> rgb; // w * h * 3

    bool empty() const { return rgb.empty(); }
    const uint8_t *at(int x, int y) const { return rgb.data() + (size_t(y) * w + x) * 3; }
};

// The thumbnails' size: the screen shrunk 4 times (256 x 224 -> 64 x 56).
constexpr int kThumbW = 64, kThumbH = 56;

// The share of pixels (0..1) that differ clearly (more than `threshold` summed over R, G and
// B). Pictures of different sizes differ everywhere.
double differing_share(const Picture &a, const Picture &b, int threshold = 48);

// How much is on the screen: distinct colours (at 4 bits a channel) and the share of pixels
// that differ clearly from the one to their right or below. A black or solid screen has 1
// colour and no edges.
struct Detail
{
    uint32_t colours = 0;
    double edges = 0;
};
Detail detail_of(const Picture &p, int threshold = 48);

uint64_t picture_hash(const Picture &p);

} // namespace corrupt
