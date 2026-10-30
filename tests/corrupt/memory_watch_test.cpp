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
