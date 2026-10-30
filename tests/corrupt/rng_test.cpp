#include "doctest/doctest.h"
#include "corrupt/rng.h"

#include <vector>

using corrupt::Rng;

static std::vector<uint64_t> take(Rng rng, int n)
{
    std::vector<uint64_t> out;
    for (int i = 0; i < n; i++)
        out.push_back(rng.next_u64());
    return out;
}

TEST_CASE("same seed, rule and firing give the same sequence")
{
    CHECK(take(Rng::for_firing(1337, 2, 40), 16) == take(Rng::for_firing(1337, 2, 40), 16));
}

TEST_CASE("seed, rule index and firing number each change the sequence")
{
    auto base = take(Rng::for_firing(1337, 2, 40), 16);
    CHECK(take(Rng::for_firing(1338, 2, 40), 16) != base);
    CHECK(take(Rng::for_firing(1337, 3, 40), 16) != base);
    CHECK(take(Rng::for_firing(1337, 2, 41), 16) != base);
}

TEST_CASE("range is inclusive and stays in bounds")
{
    Rng rng(42);
    bool saw_min = false, saw_max = false;
    for (int i = 0; i < 10000; i++)
    {
        auto v = rng.range(-3, 3);
        REQUIRE(v >= -3);
        REQUIRE(v <= 3);
        saw_min |= v == -3;
        saw_max |= v == 3;
    }
    CHECK(saw_min);
    CHECK(saw_max);
}

TEST_CASE("range with min == max returns that value")
{
    Rng rng(7);
    CHECK(rng.range(5, 5) == 5);
}

TEST_CASE("range covers the full byte span")
{
    Rng rng(9);
    std::vector<int> seen(256, 0);
    for (int i = 0; i < 20000; i++)
        seen[rng.range(0, 255)]++;
    for (int count : seen)
        CHECK(count > 0);
}

TEST_CASE("chance at the extremes")
{
    Rng rng(1);
    for (int i = 0; i < 1000; i++)
    {
        REQUIRE_FALSE(rng.chance(0.0));
        REQUIRE(rng.chance(1.0));
    }
}

TEST_CASE("chance is roughly calibrated")
{
    Rng rng(123);
    int hits = 0;
    for (int i = 0; i < 100000; i++)
        hits += rng.chance(0.25);
    CHECK(hits > 24000);
    CHECK(hits < 26000);
}
