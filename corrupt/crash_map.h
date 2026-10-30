#pragma once
#include "memory.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace corrupt
{

struct BlockCounts
{
    uint32_t crashes = 0;   // writes inside a crash's blame window
    uint32_t survivals = 0; // writes followed by a long stretch without a crash
};

// What corruption has done to a game, per 16-byte block of each memory: how often writes
// there were followed by a crash, and how often by nothing. Learned while playing and
// shared through the library (one file per person, summed when read).
class CrashMap
{
  public:
    static constexpr uint32_t kBlock = 16;
    static constexpr uint32_t kHotCrashes = 2; // one crash is never enough
    static constexpr double kHotDanger = 0.4; // 0.5 helped less on held-out seeds in FE4

    void add_crash(Target t, uint32_t offset, uint32_t n = 1);
    void add_survival(Target t, uint32_t offset, uint32_t n = 1);
    void merge(const CrashMap &other);
    void clear();
    bool empty() const;

    BlockCounts counts(Target t, uint32_t offset) const;
    double danger(Target t, uint32_t offset) const { return danger_of(counts(t, offset)); }
    bool hot(Target t, uint32_t offset) const { return hot_of(counts(t, offset)); }
    size_t hot_blocks() const; // across all targets

    // Keyed by the block's first offset, in order.
    const std::map<uint32_t, BlockCounts> &blocks(Target t) const { return per_target[size_t(t)]; }

    // A crash rate with a small prior, 0..1.
    static double danger_of(BlockCounts c) { return c.crashes / (c.crashes + c.survivals + 2.0); }
    static bool hot_of(BlockCounts c) { return c.crashes >= kHotCrashes && danger_of(c) >= kHotDanger; }

  private:
    std::array<std::map<uint32_t, BlockCounts>, kTargetCount> per_target;
};

// The hot blocks as a per-byte lookup, for the engine to check picked addresses quickly.
struct HotMask
{
    std::array<std::vector<bool>, kTargetCount> hot; // per byte, up to the last hot block

    bool is_hot(Target t, uint32_t offset) const
    {
        auto &v = hot[size_t(t)];
        return offset < v.size() && v[offset];
    }
    bool empty() const;
};
HotMask hot_mask(const CrashMap &map);

// crashmap.<user>.json: {"version": 1, "block": 16, "<target>": {"0x1F30": [crashes, survivals]}}
std::string crash_map_json(const CrashMap &map);
struct CrashMapParse
{
    CrashMap map;
    std::vector<std::string> errors;
};
CrashMapParse parse_crash_map(const std::string &text);

} // namespace corrupt
