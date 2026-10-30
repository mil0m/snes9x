#include "doctest/doctest.h"
#include "corrupt/memory_map.h"

#include <algorithm>

using namespace corrupt;

namespace
{
const MapArea *find(const std::vector<MapArea> &map, const std::string &prefix)
{
    for (auto &a : map)
        if (a.name.rfind(prefix, 0) == 0)
            return &a;
    return nullptr;
}

void check_sane(const std::vector<MapArea> &map, Target t, size_t size)
{
    for (size_t i = 0; i < map.size(); i++)
    {
        CAPTURE(map[i].name);
        CHECK(map[i].range.target == t);
        CHECK(map[i].range.first <= map[i].range.last);
        CHECK(map[i].range.last < size);
        CHECK_FALSE(map[i].name.empty());
        CHECK_FALSE(map[i].about.empty());
        if (i > 0)
            CHECK(map[i - 1].range.first <= map[i].range.first); // in address order
    }
}
} // namespace

TEST_CASE("every target has a fixed map that fits it")
{
    for (auto t : kAllTargets)
    {
        auto map = fixed_map(t, nominal_size(t));
        check_sane(map, t, nominal_size(t));
        if (t != Target::VRAM) // VRAM's layout is all live
            CHECK_FALSE(map.empty());
    }
}

TEST_CASE("the fixed maps name the hardware layout")
{
    auto oam = fixed_map(Target::OAM, 544);
    REQUIRE(oam.size() == 2);
    CHECK(oam[0].range.last == 0x1FFu);
    CHECK(oam[1].range.first == 0x200u);
    CHECK(oam[1].range.last == 0x21Fu);

    auto cgram = fixed_map(Target::CGRAM, 512);
    REQUIRE(cgram.size() == 2);
    CHECK(cgram[1].range.first == 0x100u);

    auto wram = fixed_map(Target::WRAM, 0x20000);
    auto direct = find(wram, "Direct page");
    REQUIRE(direct);
    CHECK(direct->range.last == 0xFFu);
    auto bank7f = find(wram, "Bank $7F");
    REQUIRE(bank7f);
    CHECK(bank7f->range.first == 0x10000u);

    auto aram = fixed_map(Target::ARAM, 0x10000);
    auto io = find(aram, "Sound CPU registers");
    REQUIRE(io);
    CHECK(io->range.first == 0xF0u);
    CHECK(io->range.last == 0xFFu);
    for (auto &a : wram)
        CHECK_FALSE(a.live);
}

TEST_CASE("save RAM is one area, or none when the game has no save")
{
    auto sram = fixed_map(Target::SRAM, 0x2000);
    REQUIRE(sram.size() == 1);
    CHECK(sram[0].range.last == 0x1FFFu);
    CHECK(fixed_map(Target::SRAM, 0).empty());
}

TEST_CASE("video map: mode 1 layers, their depth, and which are on screen")
{
    VideoLayout v;
    v.mode = 1;
    v.bg[0] = { .tiles = 0x0000, .map = 0x8000, .map_size = 1, .shown = true };
    v.bg[1] = { .tiles = 0x4000, .map = 0x9000, .map_size = 0, .shown = true };
    v.bg[2] = { .tiles = 0xA000, .map = 0xB000, .map_size = 0, .shown = false };
    v.bg[3] = { .tiles = 0xC000, .map = 0xD000, .map_size = 0, .shown = true }; // mode 1 has no BG4
    v.sprite_tiles = 0xC000;
    v.sprite_tiles2 = 0xE000;
    auto map = video_map(v);
    check_sane(map, Target::VRAM, 0x10000);

    auto bg1 = find(map, "BG1 tiles");
    REQUIRE(bg1);
    CHECK(bg1->name.find("4 bpp") != std::string::npos);
    CHECK(bg1->range.first == 0u);
    CHECK(bg1->range.last == 0x3FFFu); // up to BG2's tiles at 0x4000
    CHECK(bg1->live);

    auto bg1map = find(map, "BG1 tile map");
    REQUIRE(bg1map);
    CHECK(bg1map->range.first == 0x8000u);
    CHECK(bg1map->range.last == 0x8FFFu); // 64x32: two screens of 2 KB
    CHECK(bg1map->name.find("64x32") != std::string::npos);

    auto bg3 = find(map, "BG3 tiles");
    REQUIRE(bg3);
    CHECK(bg3->name.find("2 bpp") != std::string::npos);
    CHECK(bg3->about.find("not on screen") != std::string::npos);
    CHECK_FALSE(find(map, "BG4"));

    auto sprites = find(map, "Sprite tiles, first");
    REQUIRE(sprites);
    CHECK(sprites->range.first == 0xC000u);
    CHECK(sprites->range.last == 0xDFFFu);
    REQUIRE(find(map, "Sprite tiles, second"));
}

TEST_CASE("video map: areas running past the end of VRAM are clipped")
{
    VideoLayout v;
    v.mode = 3; // BG1 is 8 bpp: up to 64 KB of tiles from its base
    v.bg[0] = { .tiles = 0x8000, .map = 0x0000, .map_size = 3, .shown = true };
    v.bg[1] = { .tiles = 0x0000, .map = 0xF800, .map_size = 3, .shown = true };
    v.sprite_tiles = 0xE000;
    v.sprite_tiles2 = 0x0000; // the second table wrapped to the start
    auto map = video_map(v);
    check_sane(map, Target::VRAM, 0x10000);
    CHECK(find(map, "BG1 tiles")->range.last == 0xDFFFu); // up to the sprite tiles
    CHECK(find(map, "BG2 tile map")->range.last == 0xFFFFu);
    CHECK(find(map, "Sprite tiles, second")->range.first == 0u);
}

TEST_CASE("video map: mode 7 is one interleaved area")
{
    VideoLayout v;
    v.mode = 7;
    v.bg[0].shown = true;
    v.sprite_tiles = 0x8000;
    v.sprite_tiles2 = 0xA000;
    auto map = video_map(v);
    auto m7 = find(map, "Mode 7");
    REQUIRE(m7);
    CHECK(m7->range.first == 0u);
    CHECK(m7->range.last == 0x7FFFu);
    CHECK_FALSE(find(map, "BG2"));
}

TEST_CASE("sound map: the echo buffer and the sample directory")
{
    SoundLayout s;
    s.echo_page = 0xD0;
    s.echo_delay = 2; // 2 x 2 KB
    s.echo_writes = true;
    s.sample_dir_page = 0x30;
    auto map = sound_map(s);
    check_sane(map, Target::ARAM, 0x10000);
    auto echo = find(map, "Echo buffer");
    REQUIRE(echo);
    CHECK(echo->range.first == 0xD000u);
    CHECK(echo->range.last == 0xDFFFu);
    CHECK(echo->live);
    auto dir = find(map, "Sample directory");
    REQUIRE(dir);
    CHECK(dir->range.first == 0x3000u);
    CHECK(dir->range.last == 0x33FFu);

    s.echo_delay = 0; // the DSP still writes 4 bytes
    s.echo_writes = false;
    auto quiet_map = sound_map(s);
    auto quiet = find(quiet_map, "Echo buffer");
    REQUIRE(quiet);
    CHECK(quiet->range.last == 0xD003u);
    CHECK(quiet->about.find("off") != std::string::npos);
}

TEST_CASE("fixed and live areas merge in address order")
{
    SoundLayout s;
    s.echo_page = 0x80;
    s.echo_delay = 1;
    s.sample_dir_page = 0x02;
    auto map = merge_maps(fixed_map(Target::ARAM, 0x10000), sound_map(s));
    check_sane(map, Target::ARAM, 0x10000);
    CHECK(map.size() == 7);
}

TEST_CASE("video map: a layer's tiles end where the next area starts (FF6's field)")
{
    // FF6's field screen: tiles for BG2 and BG3 share a base, BG1's start 8 KB later.
    VideoLayout v;
    v.mode = 1;
    v.bg[0] = { .tiles = 0x6000, .map = 0x0000, .map_size = 3, .shown = true };
    v.bg[1] = { .tiles = 0x4000, .map = 0x2000, .map_size = 1, .shown = true };
    v.bg[2] = { .tiles = 0x4000, .map = 0x3000, .map_size = 1, .shown = true };
    v.sprite_tiles = 0xC000;
    v.sprite_tiles2 = 0xE000;
    auto map = video_map(v);
    CHECK(find(map, "BG1 tiles")->range.last == 0xBFFFu); // up to the sprite tiles
    CHECK(find(map, "BG2 tiles")->range.last == 0x5FFFu); // up to BG1's tiles
    CHECK(find(map, "BG3 tiles")->range.last == 0x5FFFu);
    CHECK(find(map, "BG1 tile map")->range.last == 0x1FFFu); // maps keep their exact size
}
