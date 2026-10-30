#include "tile_decode.h"

namespace corrupt
{

uint32_t tile_span(TileFormat f)
{
    switch (f)
    {
    case TileFormat::Bpp2: return 16;
    case TileFormat::Bpp4: return 32;
    case TileFormat::Bpp8: return 64;
    case TileFormat::Mode7: return 128;
    }
    return 16;
}

int tile_colours(TileFormat f)
{
    switch (f)
    {
    case TileFormat::Bpp2: return 4;
    case TileFormat::Bpp4: return 16;
    default: return 256;
    }
}

int palette_count(TileFormat f)
{
    return 256 / tile_colours(f);
}

const char *tile_format_name(TileFormat f)
{
    switch (f)
    {
    case TileFormat::Bpp2: return "2 bpp";
    case TileFormat::Bpp4: return "4 bpp";
    case TileFormat::Bpp8: return "8 bpp";
    case TileFormat::Mode7: return "mode 7";
    }
    return "";
}

const char *tile_format_id(TileFormat f)
{
    switch (f)
    {
    case TileFormat::Bpp2: return "2bpp";
    case TileFormat::Bpp4: return "4bpp";
    case TileFormat::Bpp8: return "8bpp";
    case TileFormat::Mode7: return "mode7";
    }
    return "";
}

std::optional<TileFormat> parse_tile_format(std::string_view id)
{
    for (auto f : { TileFormat::Bpp2, TileFormat::Bpp4, TileFormat::Bpp8, TileFormat::Mode7 })
        if (id == tile_format_id(f))
            return f;
    return std::nullopt;
}

std::array<uint8_t, 64> decode_tile(std::span<const uint8_t> mem, size_t offset, TileFormat f)
{
    std::array<uint8_t, 64> out{};
    auto at = [&](size_t i) -> uint8_t { return offset + i < mem.size() ? mem[offset + i] : 0; };
    if (f == TileFormat::Mode7)
    {
        for (size_t i = 0; i < 64; i++)
            out[i] = at(2 * i + 1);
        return out;
    }
    int planes = f == TileFormat::Bpp2 ? 2 : f == TileFormat::Bpp4 ? 4 : 8;
    for (int y = 0; y < 8; y++)
        for (int plane = 0; plane < planes; plane++)
        {
            uint8_t bits = at(size_t((plane / 2) * 16 + y * 2 + (plane & 1)));
            for (int x = 0; x < 8; x++)
                if (bits & (0x80 >> x))
                    out[size_t(y * 8 + x)] |= uint8_t(1 << plane);
        }
    return out;
}

Rgb snes_colour(uint16_t bgr15)
{
    auto stretch = [](int c) { return uint8_t(c << 3 | c >> 2); };
    return { stretch(bgr15 & 31), stretch(bgr15 >> 5 & 31), stretch(bgr15 >> 10 & 31) };
}

Rgb palette_colour(std::span<const uint8_t> cgram, int index)
{
    size_t at = size_t(index) * 2;
    if (index < 0 || at + 1 >= cgram.size())
        return {};
    return snes_colour(uint16_t(cgram[at] | cgram[at + 1] << 8));
}

} // namespace corrupt
