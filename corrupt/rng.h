#pragma once
#include <cstdint>

namespace corrupt
{

// SplitMix64. Small, fast and fully deterministic on every platform, which is
// what makes a recipe + seed replay identically.
class Rng
{
  public:
    explicit Rng(uint64_t state) : state(state) {}

    // An independent stream per (seed, rule, firing), so editing one rule never
    // reshuffles what the others do.
    static Rng for_firing(uint64_t seed, uint32_t rule_index, uint64_t firing_no);

    // The seed a rule actually uses: the recipe seed, re-rolled by the rule's salt.
    // A salt of 0 leaves the seed unchanged.
    static uint64_t rule_seed(uint64_t recipe_seed, uint64_t salt);

    uint64_t next_u64();
    int64_t range(int64_t min, int64_t max); // inclusive, unbiased
    bool chance(double p);

  private:
    uint64_t state;
};

} // namespace corrupt
