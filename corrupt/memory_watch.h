#pragma once
#include "engine.h"
#include "memory.h"

#include <deque>
#include <optional>
#include <vector>

namespace corrupt
{

struct MemorySnapshot
{
    Target target = Target::WRAM;
    std::vector<uint8_t> bytes;  // current contents
    std::vector<float> heat;     // how often the game changes each byte (about 50 = every frame)
    std::vector<uint32_t> hits;  // how often the recipe wrote each byte since watching (or clear_hits)
};

// Watches one memory target for the memory viewer, once per frame between frames:
// a decaying activity heat per byte, and the recipe's hits from the engine's log.
class MemoryWatch
{
  public:
    static constexpr float kDecay = 0.98f; // per frame: a byte that stops changing fades in ~2 s

    void watch(Target target, size_t size); // starts over
    void stop();
    bool watching() const { return active; }
    Target target() const { return snap.target; }

    // `frame` is the engine's current frame; events up to it that weren't seen yet are hits.
    void observe(const MemoryAccess &mem, const std::deque<LoggedEvent> &log, uint64_t frame);
    void clear_hits();

    const MemorySnapshot &snapshot() const { return snap; }

  private:
    MemorySnapshot snap;
    bool active = false;
    bool have_baseline = false;
    uint64_t seen_frame = 0;
};

// Where `haystack` holds copies of (parts of) `needle`: for finding a game's WRAM copy of
// its sprite table or palette, where corruption sticks. Runs of at least `min_run`
// matching bytes, longest first. Matches have to start from stretches with at least
// 3 different byte values, so zeros and fills never count as copies, and a run that
// mostly overlaps a longer one (a repetitive table matching itself shifted) is dropped.
struct CopyRun
{
    uint32_t haystack_offset;
    uint32_t needle_offset;
    uint32_t length;
};
std::vector<CopyRun> find_copies(const std::vector<uint8_t> &haystack, const std::vector<uint8_t> &needle,
                                 uint32_t min_run = 32);

} // namespace corrupt
