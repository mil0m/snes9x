#include "memory_map.h"

#include <algorithm>

namespace corrupt
{

namespace
{

// The added area, or null when it starts past the end.
MapArea *add(std::vector<MapArea> &map, size_t size, std::string name, Target t, uint32_t first, uint64_t last,
             std::string about, bool live = false)
{
    if (size == 0 || first >= size)
        return nullptr;
    uint32_t end = uint32_t(std::min<uint64_t>(last, size - 1));
    map.push_back({ std::move(name), { t, first, end }, std::move(about), live });
    return &map.back();
}

void tiles(MapArea *area, TileFormat format, int palette = 0)
{
    if (area)
    {
        area->tile_format = format;
        area->palette = palette;
    }
}

void sort(std::vector<MapArea> &map)
{
    std::stable_sort(map.begin(), map.end(),
                     [](const MapArea &a, const MapArea &b) { return a.range.first < b.range.first; });
}

// Bits per pixel of each background layer, per mode (0 = the mode has no such layer).
constexpr int kDepth[7][4] = {
    { 2, 2, 2, 2 }, { 4, 4, 2, 0 }, { 4, 4, 0, 0 }, { 8, 4, 0, 0 },
    { 8, 2, 0, 0 }, { 4, 2, 0, 0 }, { 4, 0, 0, 0 },
};

const char *kMapSize[4] = { "32x32", "64x32", "32x64", "64x64" };
constexpr int kScreens[4] = { 1, 2, 2, 4 };

} // namespace

std::vector<MapArea> fixed_map(Target t, size_t size)
{
    std::vector<MapArea> m;
    switch (t)
    {
    case Target::WRAM:
        add(m, size, "Direct page (usually)", t, 0x0000, 0x00FF,
            "Most games keep their busiest variables here; corruption crashes fast");
        add(m, size, "Low RAM: stack and system", t, 0x0100, 0x1FFF,
            "Mirrored into every bank; the stack usually sits near the top. Crashes fast");
        add(m, size, "Bank $7E, upper", t, 0x2000, 0xFFFF,
            "Game data: objects, buffers, sprite and palette copies");
        add(m, size, "Bank $7F", t, 0x10000, 0x1FFFF, "Big buffers: decompressed graphics, maps, sound data");
        break;
    case Target::SRAM:
        add(m, size, "Save RAM", t, 0, size ? size - 1 : 0, "The battery save; backed up automatically");
        break;
    case Target::VRAM:
        break; // all live: see video_map
    case Target::OAM:
        add(m, size, "Sprite table", t, 0x000, 0x1FF, "128 sprites x 4 bytes: x, y, tile, attributes");
        add(m, size, "Sprite size and x high bits", t, 0x200, 0x21F, "2 bits per sprite");
        break;
    case Target::CGRAM:
        add(m, size, "Background palettes", t, 0x000, 0x0FF, "Colours 0-127, 2 bytes each (15-bit)");
        add(m, size, "Sprite palettes", t, 0x100, 0x1FF, "8 palettes x 16 colours");
        break;
    case Target::ARAM:
        add(m, size, "Zero page", t, 0x0000, 0x00EF, "The sound driver's busiest variables");
        add(m, size, "Sound CPU registers", t, 0x00F0, 0x00FF, "I/O ports and timers; corruption stops the sound fast");
        add(m, size, "Stack page", t, 0x0100, 0x01FF, "The sound CPU's stack");
        add(m, size, "Driver, music and samples", t, 0x0200, 0xFFBF, "Code, song data, instruments and the echo buffer");
        add(m, size, "Boot ROM area", t, 0xFFC0, 0xFFFF, "Hidden behind the boot ROM while it's enabled");
        break;
    }
    sort(m);
    return m;
}

std::vector<MapArea> video_map(const VideoLayout &v)
{
    std::vector<MapArea> m;
    constexpr size_t kSize = 0x10000;
    auto shown = [](bool on) { return std::string(on ? "on screen" : "not on screen"); };

    if (v.mode >= 7)
        tiles(add(m, kSize, "Mode 7 tiles and map", Target::VRAM, 0x0000, 0x7FFF,
                  "Interleaved: map bytes and 8 bpp tile bytes alternate; " + shown(v.bg[0].shown), true),
              TileFormat::Mode7);
    else
        for (int i = 0; i < 4; i++)
        {
            int bpp = kDepth[v.mode][i];
            if (!bpp)
                continue;
            auto &bg = v.bg[i];
            std::string layer = "BG" + std::to_string(i + 1);
            tiles(add(m, kSize, layer + " tiles (" + std::to_string(bpp) + " bpp)", Target::VRAM, bg.tiles,
                      uint64_t(bg.tiles) + 1024u * 8u * unsigned(bpp) - 1,
                      "Up to the next area (1024 tiles at most); " + shown(bg.shown), true),
                  bpp == 2 ? TileFormat::Bpp2 : bpp == 4 ? TileFormat::Bpp4 : TileFormat::Bpp8,
                  v.mode == 0 ? i * 8 : 0); // mode 0 gives each layer its own 32 colours
            int s = bg.map_size & 3;
            add(m, kSize, layer + " tile map (" + kMapSize[s] + ")", Target::VRAM, bg.map,
                uint64_t(bg.map) + 2048u * unsigned(kScreens[s]) - 1, "Which tile goes where; " + shown(bg.shown), true);
        }

    // Sprites use colours 128-255: 4 bpp palette rows 8-15.
    tiles(add(m, kSize, "Sprite tiles, first table", Target::VRAM, v.sprite_tiles, uint64_t(v.sprite_tiles) + 0x1FFF,
              "Sprite tiles 0-255 (4 bpp)", true),
          TileFormat::Bpp4, 8);
    tiles(add(m, kSize, "Sprite tiles, second table", Target::VRAM, v.sprite_tiles2, uint64_t(v.sprite_tiles2) + 0x1FFF,
              "Sprite tiles 256-511 (4 bpp)", true),
          TileFormat::Bpp4, 8);

    // 1024 tiles is only the most a layer can use. Games pack VRAM tightly, so a layer's
    // tiles are shown up to the next area that starts after them.
    for (auto &a : m)
    {
        if (a.name.find(" tiles (") == std::string::npos)
            continue;
        for (auto &other : m)
            if (other.range.first > a.range.first && other.range.first <= a.range.last)
                a.range.last = other.range.first - 1;
    }
    sort(m);
    return m;
}

std::vector<MapArea> sound_map(const SoundLayout &s)
{
    std::vector<MapArea> m;
    constexpr size_t kSize = 0x10000;
    uint32_t echo = uint32_t(s.echo_page) << 8;
    uint32_t length = s.echo_delay ? 2048u * (s.echo_delay & 15) : 4u;
    add(m, kSize, "Echo buffer", Target::ARAM, echo, uint64_t(echo) + length - 1,
        s.echo_writes ? "The DSP writes the echo here; corrupting it smears the sound"
                      : "Echo writes are off right now",
        true);
    uint32_t dir = uint32_t(s.sample_dir_page) << 8;
    add(m, kSize, "Sample directory", Target::ARAM, dir, uint64_t(dir) + 0x3FF,
        "Where each instrument's sample starts and loops", true);
    sort(m);
    return m;
}

std::vector<MapArea> merge_maps(std::vector<MapArea> a, const std::vector<MapArea> &b)
{
    a.insert(a.end(), b.begin(), b.end());
    sort(a);
    return a;
}

} // namespace corrupt
