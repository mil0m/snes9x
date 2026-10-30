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

    TEST_CASE("danger is a crash rate with a small prior; hot needs two crashes and half")
    {
        CHECK(CrashMap::danger_of({ 0, 0 }) == 0.0);
        CHECK(CrashMap::danger_of({ 1, 0 }) == doctest::Approx(1.0 / 3));
        CHECK(CrashMap::danger_of({ 2, 2 }) == doctest::Approx(2.0 / 6));
        CHECK_FALSE(CrashMap::hot_of({ 1, 0 })); // one crash is never enough
        CHECK(CrashMap::hot_of({ 2, 0 }));       // 2 / 4
        CHECK_FALSE(CrashMap::hot_of({ 2, 1 })); // 2 / 5
        CHECK(CrashMap::hot_of({ 4, 2 }));       // 4 / 8
        CHECK_FALSE(CrashMap::hot_of({ 5, 20 }));

        CrashMap m;
        m.add_crash(Target::WRAM, 0x100, 2);
        CHECK(m.hot(Target::WRAM, 0x10F));
        CHECK_FALSE(m.hot(Target::WRAM, 0x110));
        CHECK(m.danger(Target::WRAM, 0x100) == doctest::Approx(0.5));
        CHECK(m.hot_blocks() == 1);
    }

    TEST_CASE("maps from different people add up")
    {
        CrashMap a, b;
        a.add_crash(Target::WRAM, 0x200);
        a.add_survival(Target::ARAM, 0x40, 2);
        b.add_crash(Target::WRAM, 0x205);
        b.add_survival(Target::WRAM, 0x200);
        a.merge(b);
        CHECK(a.counts(Target::WRAM, 0x200).crashes == 2);
        CHECK(a.counts(Target::WRAM, 0x200).survivals == 1);
        CHECK(a.counts(Target::ARAM, 0x40).survivals == 2);
        CHECK(a.hot(Target::WRAM, 0x200) == false); // 2 / 5
    }

    TEST_CASE("the hot mask answers per byte, for hot blocks only")
    {
        CrashMap m;
        m.add_crash(Target::WRAM, 0x1F00, 3);
        m.add_crash(Target::WRAM, 0x0800, 1); // one crash: not hot
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
        CHECK_FALSE(parse_crash_map(R"({"version":1,"block":16,"rom":{"0x10":[1,0]}})").errors.empty());
        CHECK_FALSE(parse_crash_map(R"({"version":1,"block":16,"wram":{"zz":[1,0]}})").errors.empty());
        CHECK_FALSE(parse_crash_map(R"({"version":1,"block":16,"wram":{"0x10":[-1,0]}})").errors.empty());
        CHECK_FALSE(parse_crash_map(R"({"version":1,"block":16,"wram":{"0x10":[1]}})").errors.empty());
        auto ok = parse_crash_map(R"({"version":1,"block":16,"wram":{"0x13":[1,2]}})"); // any byte of the block
        CHECK(ok.errors.empty());
        CHECK(ok.map.counts(Target::WRAM, 0x10).survivals == 2);
    }
}
