#pragma once
#include "memory.h"

#include <string>
#include <vector>

namespace corrupt
{

// One labelled stretch of a memory target, for the memory viewer's map.
struct MapArea
{
    std::string name;  // "BG1 tiles (4 bpp)"
    AddressRange range;
    std::string about; // one line on what lives there
    bool live = false; // read from the game's current settings, so it can move
};

// The hardware layout every game shares, clipped to `size`, in address order.
std::vector<MapArea> fixed_map(Target target, size_t size);

// Where the picture currently lives in VRAM, from the PPU's registers. Byte offsets.
struct VideoLayout
{
    struct Layer
    {
        uint32_t tiles = 0;   // character data
        uint32_t map = 0;     // tile map
        uint8_t map_size = 0; // 0: 32x32, 1: 64x32, 2: 32x64, 3: 64x64
        bool shown = false;   // on the main or sub screen
    };
    uint8_t mode = 0; // background mode 0-7
    Layer bg[4];
    uint32_t sprite_tiles = 0;  // first table of 256 sprite tiles
    uint32_t sprite_tiles2 = 0; // second table
};
std::vector<MapArea> video_map(const VideoLayout &video);

// Where the sound DSP keeps its echo buffer and sample directory in ARAM.
struct SoundLayout
{
    uint8_t echo_page = 0;  // ESA
    uint8_t echo_delay = 0; // EDL, 0-15: 2 KB each
    bool echo_writes = false;
    uint8_t sample_dir_page = 0; // DIR
};
std::vector<MapArea> sound_map(const SoundLayout &sound);

// Fixed and live areas together, in address order.
std::vector<MapArea> merge_maps(std::vector<MapArea> a, const std::vector<MapArea> &b);

} // namespace corrupt
