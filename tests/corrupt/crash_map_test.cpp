#include "doctest/doctest.h"
#include "corrupt/crash_map.h"

using namespace corrupt;

TEST_SUITE("crash map")
{
    TEST_CASE("counts are kept per 16-byte block")
    {
        CrashMap m;
        CHECK(m.empty());
        m.add_crash(Target::WRAM, 0x1F31);
        m.add_crash(Target::WRAM, 0x1F3F);
        m.add_survival(Target::WRAM, 0x1F30, 3);
        m.add_crash(Target::WRAM, 0x1F40);
        CHECK(m.counts(Target::WRAM, 0x1F35).crashes == 2);
        CHECK(m.counts(Target::WRAM, 0x1F35).survivals == 3);
        CHECK(m.counts(Target::WRAM, 0x1F40).crashes == 1);
        CHECK(m.counts(Target::VRAM, 0x1F35).crashes == 0); // targets are separate
        CHECK_FALSE(m.empty());
    }

    TEST_CASE("danger is a crash rate with a small prior; hot needs two crashes and 0.4")
    {
        CHECK(CrashMap::danger_of({ 0, 0 }) == 0.0);
        CHECK(CrashMap::danger_of({ 1, 0 }) == doctest::Approx(1.0 / 3));
        CHECK(CrashMap::danger_of({ 2, 2 }) == doctest::Approx(2.0 / 6));
        CHECK_FALSE(CrashMap::hot_of({ 1, 0 })); // one crash is never enough
        CHECK(CrashMap::hot_of({ 2, 0 }));       // 2 / 4
        CHECK(CrashMap::hot_of({ 2, 1 }));       // 2 / 5
        CHECK_FALSE(CrashMap::hot_of({ 2, 2 })); // 2 / 6
        CHECK(CrashMap::hot_of({ 4, 4 }));       // 4 / 10
        CHECK_FALSE(CrashMap::hot_of({ 4, 5 })); // 4 / 11
        CHECK_FALSE(CrashMap::hot_of({ 5, 20 }));

        CrashMap m;
        m.add_crash(Target::WRAM, 0x100, 2);
        CHECK(m.hot(Target::WRAM, 0x10F));
        CHECK_FALSE(m.hot(Target::WRAM, 0x110));
        CHECK(m.danger(Target::WRAM, 0x100) == doctest::Approx(0.5));
        CHECK(m.hot_blocks() == 1);
    }

    TEST_CASE("hot also needs a crash rate 3x the rest of the memory's, so noise doesn't count")
    {
        // FE4-like: crashes are rare, so a block where most writes crashed stands out.
        CrashMap rare;
        rare.add_crash(Target::WRAM, 0x320, 8);
        rare.add_survival(Target::WRAM, 0x320, 1);
        rare.add_crash(Target::WRAM, 0x330, 3);
        rare.add_survival(Target::WRAM, 0x330, 2);
        for (uint32_t b = 0x400; b < 0x2000; b += 16)
            rare.add_survival(Target::WRAM, b, 10);
        CHECK(rare.hot(Target::WRAM, 0x320));
        CHECK(rare.hot(Target::WRAM, 0x330));
        CHECK(rare.hot_blocks() == 2);

        // FF6-like: long crash loops blame almost every block, so the top of the noise
        // (9 crashes, 9 survivals: danger 0.45) is only ~2.5x the rest and isn't hot.
        CrashMap noisy;
        noisy.add_crash(Target::WRAM, 0x15E0, 9);
        noisy.add_survival(Target::WRAM, 0x15E0, 9);
        for (uint32_t b = 0x400; b < 0x1000; b += 16)
        {
            noisy.add_crash(Target::WRAM, b, 2);
            noisy.add_survival(Target::WRAM, b, 8);
        }
        CHECK(noisy.danger(Target::WRAM, 0x15E0) >= CrashMap::kHotDanger);
        CHECK_FALSE(noisy.hot(Target::WRAM, 0x15E0));
        CHECK(noisy.hot_blocks() == 0);
        CHECK(hot_mask(noisy).empty());

        // The rate is the other blocks', so a block alone, or with only survivors, can be hot.
        CrashMap alone;
        alone.add_crash(Target::WRAM, 0x100, 3);
        CHECK(alone.hot(Target::WRAM, 0x100));
        // Per memory: crashes in ARAM don't raise the bar in WRAM.
        alone.add_crash(Target::ARAM, 0x100, 50);
        CHECK(alone.hot(Target::WRAM, 0x100));
        // Every write crashed so far: nothing stands out until something survives.
        CrashMap early;
        early.add_crash(Target::WRAM, 0x100, 2);
        early.add_crash(Target::WRAM, 0x200, 1);
        CHECK_FALSE(early.hot(Target::WRAM, 0x100));
        early.add_survival(Target::WRAM, 0x200, 5);
        CHECK(early.hot(Target::WRAM, 0x100));
    }

    TEST_CASE("maps from different people add up")
    {
        CrashMap a, b;
        a.add_crash(Target::WRAM, 0x200);
        a.add_survival(Target::ARAM, 0x40, 2);
        b.add_crash(Target::WRAM, 0x205);
        b.add_survival(Target::WRAM, 0x200, 2);
        a.merge(b);
        CHECK(a.counts(Target::WRAM, 0x200).crashes == 2);
        CHECK(a.counts(Target::WRAM, 0x200).survivals == 2);
        CHECK(a.counts(Target::ARAM, 0x40).survivals == 2);
        CHECK(a.hot(Target::WRAM, 0x200) == false); // 2 / 6
    }

    TEST_CASE("the hot mask answers per byte, for hot blocks only")
    {
        CrashMap m;
        m.add_crash(Target::WRAM, 0x1F00, 3);
        m.add_crash(Target::WRAM, 0x0800, 1); // one crash: not hot
        m.add_survival(Target::WRAM, 0x0800, 9);
        m.add_crash(Target::OAM, 0x10, 2);
        HotMask mask = hot_mask(m);
        CHECK(mask.is_hot(Target::WRAM, 0x1F00));
        CHECK(mask.is_hot(Target::WRAM, 0x1F0F));
        CHECK_FALSE(mask.is_hot(Target::WRAM, 0x1F10));
        CHECK_FALSE(mask.is_hot(Target::WRAM, 0x0800));
        CHECK_FALSE(mask.is_hot(Target::WRAM, 0x1FFFF)); // past the last hot block
        CHECK(mask.is_hot(Target::OAM, 0x1F));
        CHECK_FALSE(mask.is_hot(Target::VRAM, 0x1F00));
        CHECK_FALSE(HotMask{}.is_hot(Target::WRAM, 0));
        CHECK(HotMask{}.empty());
        CHECK_FALSE(mask.empty());
    }

    TEST_CASE("a block can be added to a mask by hand, by any of its bytes")
    {
        HotMask mask;
        mask.add_block(Target::WRAM, 0x0325); // the block 0x0320-0x032F
        CHECK(mask.is_hot(Target::WRAM, 0x0320));
        CHECK(mask.is_hot(Target::WRAM, 0x032F));
        CHECK_FALSE(mask.is_hot(Target::WRAM, 0x031F));
        CHECK_FALSE(mask.is_hot(Target::WRAM, 0x0330));
        CHECK_FALSE(mask.is_hot(Target::WRAM, 0x3200));
    }

    TEST_CASE("JSON: readable, sorted, and round-trips")
    {
        CrashMap m;
        m.add_survival(Target::WRAM, 0x1F35, 7);
        m.add_crash(Target::WRAM, 0x1F35, 2);
        m.add_crash(Target::WRAM, 0x0010);
        m.add_survival(Target::ARAM, 0xEF00);
        std::string text = crash_map_json(m);
        CHECK(text == "{\n"
                      "  \"version\": 1,\n"
                      "  \"block\": 16,\n"
                      "  \"wram\": {\n"
                      "    \"0x0010\": [1, 0],\n"
                      "    \"0x1F30\": [2, 7]\n"
                      "  },\n"
                      "  \"aram\": {\n"
                      "    \"0xEF00\": [0, 1]\n"
                      "  }\n"
                      "}\n");
        auto back = parse_crash_map(text);
        REQUIRE(back.errors.empty());
        CHECK(crash_map_json(back.map) == text);
        CHECK(crash_map_json(CrashMap{}) == "{\n  \"version\": 1,\n  \"block\": 16\n}\n");
    }

    TEST_CASE("JSON: problems are reported, not guessed at")
    {
        CHECK_FALSE(parse_crash_map("not json").errors.empty());
        CHECK_FALSE(parse_crash_map(R"({"version":1,"block":32})").errors.empty());
        CHECK_FALSE(parse_crash_map(R"({"version":1,"block":16,"dsp":{"0x10":[1,0]}})").errors.empty());
        CHECK_FALSE(parse_crash_map(R"({"version":1,"block":16,"wram":{"zz":[1,0]}})").errors.empty());
        CHECK_FALSE(parse_crash_map(R"({"version":1,"block":16,"wram":{"0x10":[-1,0]}})").errors.empty());
        CHECK_FALSE(parse_crash_map(R"({"version":1,"block":16,"wram":{"0x10":[1]}})").errors.empty());
        CHECK_FALSE(parse_crash_map(R"({"version":1,"block":"16"})").errors.empty()); // no throw
        CHECK_FALSE(parse_crash_map(R"([1,2])").errors.empty());
        auto ok = parse_crash_map(R"({"version":1,"block":16,"wram":{"0x13":[1,2]}})"); // any byte of the block
        CHECK(ok.errors.empty());
        CHECK(ok.map.counts(Target::WRAM, 0x10).survivals == 2);
    }

    TEST_CASE("JSON: blocks past the end of memory are dropped, so a bad file can't blow up the mask")
    {
        // A hand edit or another person's broken file: a hot block at the top of the
        // 32-bit range would make the mask 4 G entries, or overflow its size to 0.
        auto far = parse_crash_map(R"({"version":1,"block":16,"wram":{"0x20000":[9,0],"0xFFFFFFF0":[9,0],"0x1FFF0":[9,0]},)"
                                   R"("cgram":{"0x200":[9,0]},"sram":{"0x7FFF0":[9,0],"0x80000":[9,0]},)"
                                   R"("vram":{"0x100000000":[9,0]}})");
        CHECK(far.errors.size() == 5);
        CHECK(far.map.blocks(Target::WRAM).size() == 1); // 0x1FFF0, the last WRAM block
        CHECK(far.map.blocks(Target::CGRAM).empty());
        CHECK(far.map.blocks(Target::SRAM).size() == 1); // SRAM goes up to 512 KB
        CHECK(far.map.blocks(Target::VRAM).empty());     // not truncated to 0x0
        auto mask = hot_mask(far.map);
        CHECK(mask.hot[size_t(Target::WRAM)].size() == 0x20000);
        CHECK(mask.is_hot(Target::WRAM, 0x1FFFF));
    }
}
