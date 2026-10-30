#pragma once
#include "byte_colour.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace corrupt
{

// How the SNES stores 8x8 graphics tiles. Planar formats keep each row's bit planes in
// pairs (planes 0-1, then 2-3 sixteen bytes on, and so on), leftmost pixel in bit 7.
// Mode 7 keeps one byte per pixel in the odd bytes of VRAM, the even ones being its map.
enum class TileFormat
{
    Bpp2,
    Bpp4,
    Bpp8,
    Mode7,
};

uint32_t tile_span(TileFormat f);   // bytes one tile covers: 16, 32, 64 or 128
int tile_colours(TileFormat f);     // 4, 16 or 256
int palette_count(TileFormat f);    // how many palette rows CGRAM holds for it: 64, 16 or 1
const char *tile_format_name(TileFormat f); // "2 bpp", "4 bpp", "8 bpp", "mode 7"
const char *tile_format_id(TileFormat f);   // "2bpp", "4bpp", "8bpp", "mode7"
std::optional<TileFormat> parse_tile_format(std::string_view id);

// The 64 colour indices of the tile starting at `offset`, row by row. Bytes past the end
// of `mem` read as 0.
std::array<uint8_t, 64> decode_tile(std::span<const uint8_t> mem, size_t offset, TileFormat f);

Rgb snes_colour(uint16_t bgr15);                              // 15-bit BGR, bit 15 ignored
Rgb palette_colour(std::span<const uint8_t> cgram, int index); // black past the end

} // namespace corrupt
