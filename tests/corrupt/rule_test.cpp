#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/rule.h"

#include <bit>
#include <set>

using namespace corrupt;

namespace
{
// Every firing adds 1 to `count` bytes, so after N firings the touched bytes show who got hit.
Rule incrementer(uint32_t count, Pick pick)
{
    Rule r;
    r.name = "inc";
    r.range = { Target::WRAM, 0x100, 0x1FF };
    r.op = { .type = OpType::Add, .min = 1, .max = 1 };
    r.when = { .type = TriggerType::EveryFrames, .every = 1 };
    r.count = count;
    r.pick = pick;
    return r;
}

std::set<uint32_t> touched(FakeMemory &mem)
{
    std::set<uint32_t> out;
    auto &w = mem.of(Target::WRAM);
    for (uint32_t a = 0; a < w.size(); a++)
        if (w[a])
            out.insert(a);
    return out;
}

void run(const Rule &r, RuleRuntime &rt, FakeMemory &mem, int frames, uint64_t seed = 42)
{
    for (int f = 1; f <= frames; f++)
        tick_rule(r, rt, mem, seed, 0, uint64_t(f), 0);
}
} // namespace

TEST_CASE("fresh picks new addresses each firing")
{
    FakeMemory mem;
    RuleRuntime rt;
    run(incrementer(3, Pick::Fresh), rt, mem, 20);
    CHECK(touched(mem).size() > 3);
    CHECK(rt.firings == 20);
}

TEST_CASE("sticky keeps hitting the same addresses")
{
    FakeMemory mem;
    RuleRuntime rt;
    run(incrementer(3, Pick::Sticky), rt, mem, 20);
    auto hit = touched(mem);
    CHECK(hit.size() == 3);
    for (auto a : hit)
        CHECK(mem.of(Target::WRAM)[a] == 20);
}

TEST_CASE("addresses within one firing are distinct")
{
    FakeMemory mem;
    RuleRuntime rt;
    auto r = incrementer(200, Pick::Fresh);
    r.range = { Target::WRAM, 0x100, 0x1FF }; // 256 bytes, 200 picks
    tick_rule(r, rt, mem, 1, 0, 1, 0);
    CHECK(touched(mem).size() == 200);
}

TEST_CASE("count larger than the range hits every byte once")
{
    FakeMemory mem;
    RuleRuntime rt;
    auto r = incrementer(1000, Pick::Fresh);
    r.range = { Target::WRAM, 0x10, 0x1F };
    tick_rule(r, rt, mem, 1, 0, 1, 0);
    CHECK(touched(mem).size() == 16);
    CHECK(mem.of(Target::WRAM)[0x10] == 1);
}

TEST_CASE("addresses never leave the range")
{
    FakeMemory mem;
    RuleRuntime rt;
    run(incrementer(5, Pick::Fresh), rt, mem, 200);
    for (auto a : touched(mem))
    {
        CHECK(a >= 0x100);
        CHECK(a <= 0x1FF);
    }
}

TEST_CASE("disabled rules and zero intensity write nothing")
{
    FakeMemory mem;
    RuleRuntime rt;
    auto off = incrementer(3, Pick::Fresh);
    off.enabled = false;
    run(off, rt, mem, 10);
    auto quiet = incrementer(3, Pick::Fresh);
    quiet.intensity = 0.0;
    run(quiet, rt, mem, 10);
    CHECK(mem.writes == 0);
    CHECK(rt.firings == 0);
}

TEST_CASE("intensity scales count, with a floor of one address")
{
    FakeMemory mem;
    RuleRuntime rt;
    auto r = incrementer(10, Pick::Fresh);
    r.intensity = 0.5;
    auto events = tick_rule(r, rt, mem, 1, 0, 1, 0);
    CHECK(events.size() == 5);

    r.intensity = 0.01;
    events = tick_rule(r, rt, mem, 1, 0, 2, 0);
    CHECK(events.size() == 1);
}

TEST_CASE("intensity scales add deltas and bitflip bits")
{
    FakeMemory mem;
    RuleRuntime rt;
    auto r = incrementer(1, Pick::Sticky);
    r.op = { .type = OpType::Add, .min = 10, .max = 10 };
    r.intensity = 0.5;
    auto events = tick_rule(r, rt, mem, 1, 0, 1, 0);
    REQUIRE(events.size() == 1);
    CHECK(events[0].new_value == 5);

    Rule flip = incrementer(1, Pick::Fresh);
    flip.op = { .type = OpType::Bitflip, .bits = 8 };
    flip.intensity = 0.25;
    FakeMemory m2;
    RuleRuntime rt2;
    events = tick_rule(flip, rt2, m2, 1, 0, 1, 0);
    REQUIRE(events.size() == 1);
    CHECK(std::popcount(events[0].new_value) == 2);
}

TEST_CASE("sticky addresses survive intensity changes")
{
    FakeMemory mem;
    RuleRuntime rt;
    auto r = incrementer(4, Pick::Sticky);
    r.intensity = 0.5;
    run(r, rt, mem, 5);
    auto low = touched(mem);
    r.intensity = 1.0;
    run(r, rt, mem, 5);
    auto high = touched(mem);
    CHECK(low.size() == 2);
    CHECK(high.size() == 4);
    for (auto a : low)
        CHECK(high.count(a) == 1);
}

TEST_CASE("the same seed replays identically, a different seed doesn't")
{
    FakeMemory a, b, c;
    RuleRuntime ra, rb, rc;
    auto r = incrementer(3, Pick::Fresh);
    r.when = { .type = TriggerType::ChancePerFrame, .chance = 0.3 };
    run(r, ra, a, 300, 7);
    run(r, rb, b, 300, 7);
    run(r, rc, c, 300, 8);
    CHECK(a.of(Target::WRAM) == b.of(Target::WRAM));
    CHECK(a.of(Target::WRAM) != c.of(Target::WRAM));
}

TEST_CASE("validate_rule reports the failing part")
{
    FakeMemory mem;
    auto r = incrementer(3, Pick::Fresh);
    CHECK_FALSE(validate_rule(r, mem));

    r.range = { Target::CGRAM, 0, 0x300 };
    auto err = validate_rule(r, mem);
    REQUIRE(err);
    CHECK(err->find("range") != std::string::npos);

    r = incrementer(0, Pick::Fresh);
    CHECK(validate_rule(r, mem));

    r = incrementer(1, Pick::Fresh);
    r.intensity = 1.5;
    CHECK(validate_rule(r, mem));
}
