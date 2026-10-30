#include "doctest/doctest.h"
#include "corrupt/tile_decode.h"

#include <vector>

using namespace corrupt;

namespace
{
// Builds the planar bytes for one tile from its 64 colour indices, the way the SNES stores
// them: rows of bit planes in pairs, leftmost pixel in bit 7.
std::vector<uint8_t> planar(const std::array<uint8_t, 64> &pixels, int bpp)
{
    std::vector<uint8_t> out(size_t(bpp) * 8, 0);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            for (int plane = 0; plane < bpp; plane++)
                if (pixels[size_t(y * 8 + x)] >> plane & 1)
                    out[size_t((plane / 2) * 16 + y * 2 + (plane & 1))] |= uint8_t(0x80 >> x);
    return out;
}

std::array<uint8_t, 64> pattern(int colours)
{
    std::array<uint8_t, 64> p{};
    for (int i = 0; i < 64; i++)
        p[size_t(i)] = uint8_t((i * 7 + i / 8) % colours);
    return p;
}
} // namespace

TEST_CASE("tile sizes and colours per format")
{
    CHECK(tile_span(TileFormat::Bpp2) == 16u);
    CHECK(tile_span(TileFormat::Bpp4) == 32u);
    CHECK(tile_span(TileFormat::Bpp8) == 64u);
    CHECK(tile_span(TileFormat::Mode7) == 128u);
    CHECK(tile_colours(TileFormat::Bpp2) == 4);
    CHECK(tile_colours(TileFormat::Bpp4) == 16);
    CHECK(tile_colours(TileFormat::Mode7) == 256);
    CHECK(palette_count(TileFormat::Bpp2) == 64);
    CHECK(palette_count(TileFormat::Bpp4) == 16);
    CHECK(palette_count(TileFormat::Bpp8) == 1);
    CHECK(std::string(tile_format_name(TileFormat::Bpp4)) == "4 bpp");
    CHECK(parse_tile_format("mode7") == TileFormat::Mode7);
    CHECK(parse_tile_format("2bpp") == TileFormat::Bpp2);
    CHECK_FALSE(parse_tile_format("3bpp"));
}

TEST_CASE("planar tiles decode back to their pixels")
{
    for (auto [format, bpp] : { std::pair{ TileFormat::Bpp2, 2 }, std::pair{ TileFormat::Bpp4, 4 },
                                std::pair{ TileFormat::Bpp8, 8 } })
    {
        CAPTURE(bpp);
        auto pixels = pattern(1 << bpp);
        auto bytes = planar(pixels, bpp);
        std::vector<uint8_t> mem(5, 0xEE); // the tile starts at offset 5
        mem.insert(mem.end(), bytes.begin(), bytes.end());
        CHECK(decode_tile(mem, 5, format) == pixels);
    }
}

TEST_CASE("the SNES layout: bit 7 is the left pixel, planes 2-3 sit 16 bytes on")
{
    std::vector<uint8_t> mem(32, 0);
    mem[0] = 0x80;  // row 0, plane 0: the leftmost pixel
    mem[17] = 0x01; // row 0, plane 3: the rightmost pixel
    auto t = decode_tile(mem, 0, TileFormat::Bpp4);
    CHECK(t[0] == 1);
    CHECK(t[7] == 8);
    CHECK(t[1] == 0);
}

TEST_CASE("mode 7 tiles are one byte per pixel in the odd bytes")
{
    std::vector<uint8_t> mem(256, 0xAA); // map bytes everywhere
    for (int i = 0; i < 64; i++)
        mem[size_t(128 + 2 * i + 1)] = uint8_t(i + 100); // tile 1
    auto t = decode_tile(mem, 128, TileFormat::Mode7);
    for (int i = 0; i < 64; i++)
        CHECK(t[size_t(i)] == i + 100);
}

TEST_CASE("bytes past the end read as zero")
{
    std::vector<uint8_t> mem(20, 0xFF); // a 4 bpp tile needs 32
    auto t = decode_tile(mem, 0, TileFormat::Bpp4);
    CHECK(t[0] == 15);  // row 0: all four planes are inside
    CHECK(t[56] == 3);  // row 7: planes 2 and 3 (bytes 30-31) are cut off
    auto none = decode_tile(mem, 1000, TileFormat::Bpp2);
    CHECK(none == std::array<uint8_t, 64>{});
}

TEST_CASE("palette colours: 15-bit BGR, little-endian, stretched to 8 bits")
{
    std::vector<uint8_t> cgram(512, 0);
    cgram[2] = 0x1F; // colour 1: pure red, 0x001F
    cgram[4] = 0xE0; // colour 2: pure green, 0x03E0
    cgram[5] = 0x03;
    cgram[6] = 0x00; // colour 3: pure blue, 0x7C00
    cgram[7] = 0x7C;
    cgram[8] = 0xFF; // colour 4: white, bit 15 ignored
    cgram[9] = 0xFF;
    CHECK(palette_colour(cgram, 0) == Rgb{ 0, 0, 0 });
    CHECK(palette_colour(cgram, 1) == Rgb{ 255, 0, 0 });
    CHECK(palette_colour(cgram, 2) == Rgb{ 0, 255, 0 });
    CHECK(palette_colour(cgram, 3) == Rgb{ 0, 0, 255 });
    CHECK(palette_colour(cgram, 4) == Rgb{ 255, 255, 255 });
    CHECK(snes_colour(0x0010) == Rgb{ 132, 0, 0 });
    CHECK(palette_colour(cgram, 300) == Rgb{ 0, 0, 0 }); // past the end
}
