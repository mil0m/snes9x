#pragma once
#include "crash_map.h"
#include "engine.h"

#include <deque>
#include <unordered_map>

namespace corrupt
{

// Turns the engine's writes into crash map counts. Call observe() between frames, after
// the frame ran; when the guard rolls back, call crash() with its blame window.
//
// Counts are per episode, not per write: a crash counts once for every block its window
// touched, and a block's survivals count at most once every kSurvivalSpacing frames, so a
// rule hitting the same block every frame doesn't drown out the crashes.
class CrashLearner
{
  public:
    static constexpr uint64_t kSurviveFrames = 900;   // 15 s without a crash
    static constexpr uint64_t kSurvivalSpacing = 300; // per block

    // Collects writes from frames not seen yet, and turns writes old enough into
    // survivals. A frame earlier than the last one means the run was rewound or restarted:
    // writes after it never happened.
    void observe(const std::deque<LoggedEvent> &log, uint64_t frame, CrashMap &map);

    // The guard blamed writes in frames (after, through] and rolled back to `restored`.
    void crash(uint64_t after, uint64_t through, uint64_t restored, CrashMap &map);

    void reset(); // a new game
    size_t pending() const { return writes.size(); }

  private:
    struct Write
    {
        uint64_t frame;
        Target target;
        uint32_t offset;
    };
    static uint64_t key(Target t, uint32_t offset) { return uint64_t(t) << 32 | offset / CrashMap::kBlock; }
    void forget_after(uint64_t frame);

    std::deque<Write> writes; // in frame order
    uint64_t seen = 0;        // the last frame collected
    std::unordered_map<uint64_t, uint64_t> last_survival; // block -> frame of the write last credited
};

} // namespace corrupt
