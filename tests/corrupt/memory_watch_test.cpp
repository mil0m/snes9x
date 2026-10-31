#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/memory_watch.h"

using namespace corrupt;

namespace
{
LoggedEvent hit(uint64_t frame, Target t, uint32_t offset)
{
    return { frame, 0, { t, offset, 0, 1 } };
}
} // namespace

TEST_CASE("a byte that changes every frame heats up toward 50, then fades when it stops")
{
    FakeMemory mem;
    MemoryWatch w;
    w.watch(Target::WRAM, mem.size(Target::WRAM));
    std::deque<LoggedEvent> log;
    for (uint64_t f = 1; f <= 600; f++)
    {
        mem.of(Target::WRAM)[0x40]++;
        w.observe(mem, log, f);
    }
    auto s = w.snapshot();
    CHECK(s.heat[0x40] > 45.0f);
    CHECK(s.heat[0x40] <= 50.0f);
    CHECK(s.heat[0x41] == 0.0f);
    CHECK(s.bytes[0x40] == mem.of(Target::WRAM)[0x40]);

    for (uint64_t f = 601; f <= 720; f++) // two quiet seconds
        w.observe(mem, log, f);
    CHECK(w.snapshot().heat[0x40] < 5.0f);
}

TEST_CASE("a byte that changes once a second stays visible but cooler")
{
    FakeMemory mem;
    MemoryWatch w;
    w.watch(Target::WRAM, mem.size(Target::WRAM));
    std::deque<LoggedEvent> log;
    for (uint64_t f = 1; f <= 600; f++)
    {
        if (f % 60 == 0)
            mem.of(Target::WRAM)[0x80]++;
        w.observe(mem, log, f);
    }
    float h = w.snapshot().heat[0x80];
    CHECK(h > 0.2f);
    CHECK(h < 2.0f);
}

TEST_CASE("the first look is only a baseline")
{
    FakeMemory mem;
    mem.of(Target::WRAM)[5] = 9;
    MemoryWatch w;
    w.watch(Target::WRAM, mem.size(Target::WRAM));
    w.observe(mem, {}, 1);
    CHECK(w.snapshot().heat[5] == 0.0f);
}

TEST_CASE("hits count this target's new events only, and survive a rollback")
{
    FakeMemory mem;
    MemoryWatch w;
    w.watch(Target::CGRAM, mem.size(Target::CGRAM));
    std::deque<LoggedEvent> log{ hit(1, Target::CGRAM, 4), hit(1, Target::WRAM, 4), hit(2, Target::CGRAM, 4) };
    w.observe(mem, log, 2);
    w.observe(mem, log, 3); // the same events again: not counted twice
    CHECK(w.snapshot().hits[4] == 2u);

    // A rollback to frame 1: the log is trimmed, then new events arrive.
    log = { hit(1, Target::CGRAM, 4), hit(2, Target::CGRAM, 6) };
    w.observe(mem, log, 1);
    w.observe(mem, log, 2);
    auto s = w.snapshot();
    CHECK(s.hits[4] == 2u);
    CHECK(s.hits[6] == 1u);

    w.clear_hits();
    CHECK(w.snapshot().hits[4] == 0u);
}

TEST_CASE("switching target starts over, and stopping drops everything")
{
    FakeMemory mem;
    MemoryWatch w;
    CHECK_FALSE(w.watching());
    w.watch(Target::WRAM, mem.size(Target::WRAM));
    for (uint64_t f = 1; f <= 30; f++)
    {
        mem.of(Target::WRAM)[1]++;
        w.observe(mem, {}, f);
    }
    w.watch(Target::OAM, mem.size(Target::OAM));
    auto s = w.snapshot();
    CHECK(s.target == Target::OAM);
    CHECK(s.bytes.size() == 544);
    CHECK(s.heat.size() == 544);
    CHECK(s.hits.size() == 544);
    CHECK(s.heat[1] == 0.0f);
    w.stop();
    CHECK_FALSE(w.watching());
    CHECK(w.snapshot().bytes.empty());
}

TEST_CASE("a target that shrank (another game) is handled")
{
    FakeMemory mem;
    mem.of(Target::SRAM).assign(16, 0);
    MemoryWatch w;
    w.watch(Target::SRAM, 0x2000); // sized for the last game
    w.observe(mem, {}, 1);
    w.observe(mem, {}, 2);
    CHECK(w.snapshot().bytes.size() == 16);
}

TEST_CASE("the ROM: no activity heat, bytes that follow the edits, hits only once there are some")
{
    FakeMemory mem;
    MemoryWatch w;
    w.watch(Target::ROM, mem.size(Target::ROM));
    w.observe(mem, {}, 1);
    auto &s = w.snapshot();
    CHECK(s.bytes.size() == mem.size(Target::ROM));
    CHECK(s.heat.empty()); // only rules change the ROM; the game never does
    CHECK(s.hits.empty()); // no 4 bytes a byte until something hits

    mem.of(Target::ROM)[0x1234] = 0x77;
    std::deque<LoggedEvent> log{ hit(2, Target::ROM, 0x1234) };
    w.observe(mem, log, 2);
    CHECK(w.snapshot().bytes[0x1234] == 0x77);
    REQUIRE(w.snapshot().hits.size() == mem.size(Target::ROM));
    CHECK(w.snapshot().hits[0x1234] == 1u);
    CHECK(w.snapshot().heat.empty());
}

TEST_SUITE("the game's reads and writes")
{
    TEST_CASE("counts become an average per frame, and the counters start over")
    {
        FakeMemory mem;
        MemoryWatch w;
        w.watch(Target::WRAM, mem.size(Target::WRAM));
        std::vector<uint32_t> reads(mem.size(Target::WRAM)), writes(reads.size());
        for (int f = 0; f < 600; f++)
        {
            reads[0x40] += 3;
            writes[0x41] += 1;
            if (f % 60 == 0)
                reads[0x80] += 1; // once a second
            w.observe_accesses(reads, writes);
            REQUIRE(reads[0x40] == 0u);
            REQUIRE(writes[0x41] == 0u);
        }
        auto &s = w.snapshot();
        REQUIRE(s.reads.size() == reads.size());
        REQUIRE(s.writes.size() == reads.size());
        CHECK(s.reads[0x40] == doctest::Approx(3.0).epsilon(0.01));
        CHECK(s.writes[0x40] == 0.0f);
        CHECK(s.writes[0x41] == doctest::Approx(1.0).epsilon(0.01));
        CHECK(s.reads[0x80] > 0.005f); // visible, though rare
        CHECK(s.reads[0x80] < 0.05f);
        CHECK(s.reads[0x81] == 0.0f);

        for (int f = 0; f < 300; f++) // five quiet seconds
            w.observe_accesses(reads, writes);
        CHECK(w.snapshot().reads[0x40] < 0.01f);
    }

    TEST_CASE("only WRAM has them; other targets just drop the counts")
    {
        FakeMemory mem;
        MemoryWatch w;
        w.watch(Target::VRAM, mem.size(Target::VRAM));
        std::vector<uint32_t> reads(0x20000, 5), writes(0x20000, 5);
        w.observe_accesses(reads, writes);
        CHECK(w.snapshot().reads.empty());
        CHECK(w.snapshot().writes.empty());
        CHECK(reads[7] == 0u);
        CHECK(writes[7] == 0u);

        w.watch(Target::WRAM, mem.size(Target::WRAM));
        reads[7] = 60;
        w.observe_accesses(reads, writes);
        CHECK(w.snapshot().reads[7] > 0.0f);
        w.watch(Target::WRAM, mem.size(Target::WRAM)); // starting over forgets them
        CHECK(w.snapshot().reads.empty());
    }
}

TEST_SUITE("finding copies")
{
    std::vector<uint8_t> palette_like(size_t n, uint32_t seed)
    {
        std::vector<uint8_t> out(n);
        for (size_t i = 0; i < n; i++)
        {
            seed = seed * 1664525u + 1013904223u;
            out[i] = uint8_t(seed >> 24);
        }
        return out;
    }

    void put(std::vector<uint8_t> & hay, size_t at, const std::vector<uint8_t> &src, size_t from, size_t len)
    {
        std::copy(src.begin() + long(from), src.begin() + long(from + len), hay.begin() + long(at));
    }

    TEST_CASE("a whole copy is found at its offset")
    {
        auto needle = palette_like(512, 1);
        std::vector<uint8_t> hay(0x20000, 0);
        put(hay, 0x3000, needle, 0, 512);
        auto runs = find_copies(hay, needle, 32);
        REQUIRE(runs.size() == 1);
        CHECK(runs[0].haystack_offset == 0x3000u);
        CHECK(runs[0].needle_offset == 0u);
        CHECK(runs[0].length == 512u);
    }

    TEST_CASE("a partial copy reports which part of the needle it holds")
    {
        auto needle = palette_like(512, 2);
        std::vector<uint8_t> hay(0x20000, 0);
        put(hay, 0x5000, needle, 100, 200);
        auto runs = find_copies(hay, needle, 32);
        REQUIRE(runs.size() == 1);
        CHECK(runs[0].haystack_offset == 0x5000u);
        CHECK(runs[0].needle_offset == 100u);
        CHECK(runs[0].length == 200u);
    }

    TEST_CASE("zeros and single-value stretches never count as copies")
    {
        std::vector<uint8_t> needle(512, 0);
        auto tail = palette_like(16, 3);
        std::copy(tail.begin(), tail.end(), needle.begin() + 300); // only 16 interesting bytes
        std::vector<uint8_t> hay(0x20000, 0);
        std::fill(hay.begin() + 0x100, hay.begin() + 0x400, 0x7F);
        CHECK(find_copies(hay, needle, 32).empty());
    }

    TEST_CASE("short matches are ignored; several copies come longest first")
    {
        auto needle = palette_like(544, 4);
        std::vector<uint8_t> hay(0x20000, 0);
        put(hay, 0x100, needle, 0, 20);   // too short
        put(hay, 0x8000, needle, 0, 64);
        put(hay, 0xA000, needle, 0, 544);
        auto runs = find_copies(hay, needle, 32);
        REQUIRE(runs.size() == 2);
        CHECK(runs[0].haystack_offset == 0xA000u);
        CHECK(runs[0].length == 544u);
        CHECK(runs[1].haystack_offset == 0x8000u);
        CHECK(runs[1].length == 64u);
    }

    TEST_CASE("one changed byte splits a copy into two runs")
    {
        auto needle = palette_like(512, 5);
        std::vector<uint8_t> hay(0x20000, 0);
        put(hay, 0x2000, needle, 0, 512);
        hay[0x2000 + 256] ^= 0xFF;
        auto runs = find_copies(hay, needle, 32);
        REQUIRE(runs.size() == 2);
        CHECK(runs[0].length + runs[1].length == 511u);
    }

    TEST_CASE("a repetitive table is reported once, not once per shift")
    {
        // Sprite tables repeat: 4-byte entries that often look alike.
        std::vector<uint8_t> needle;
        for (int i = 0; i < 136; i++)
            for (uint8_t b : { uint8_t(0x10 + i % 3), uint8_t(0x80), uint8_t(0x21), uint8_t(0x30) })
                needle.push_back(b);
        std::vector<uint8_t> hay(0x20000, 0);
        put(hay, 0x300, needle, 0, needle.size());
        auto runs = find_copies(hay, needle, 32);
        REQUIRE(runs.size() == 1);
        CHECK(runs[0].haystack_offset == 0x300u);
        CHECK(runs[0].length == 544u);
    }
}
