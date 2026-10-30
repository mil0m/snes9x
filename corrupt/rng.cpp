#include "rng.h"

namespace corrupt
{

static uint64_t mix(uint64_t z)
{
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

Rng Rng::for_firing(uint64_t seed, uint32_t rule_index, uint64_t firing_no)
{
    uint64_t state = mix(seed);
    state = mix(state ^ (uint64_t(rule_index) + 0x9e3779b97f4a7c15ULL));
    state = mix(state ^ (firing_no + 0x632be59bd9b4e019ULL));
    return Rng(state);
}

uint64_t Rng::rule_seed(uint64_t recipe_seed, uint64_t salt)
{
    return salt ? recipe_seed ^ mix(salt) : recipe_seed;
}

uint64_t Rng::next_u64()
{
    state += 0x9e3779b97f4a7c15ULL;
    return mix(state);
}

int64_t Rng::range(int64_t min, int64_t max)
{
    if (max <= min)
        return min;

    uint64_t span = uint64_t(max - min) + 1;
    if (span == 0) // the full 64-bit range
        return int64_t(next_u64());

    // Reject the top sliver so every value is equally likely.
    uint64_t limit = UINT64_MAX - (UINT64_MAX % span);
    uint64_t v;
    do
        v = next_u64();
    while (v >= limit);

    return min + int64_t(v % span);
}

bool Rng::chance(double p)
{
    if (p <= 0.0)
        return false;
    if (p >= 1.0)
        return true;
    return double(next_u64() >> 11) * 0x1.0p-53 < p;
}

} // namespace corrupt
