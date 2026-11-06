#pragma once
#include "explore.h"

#include <bitset>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

// Normal play's range (#81): what a moment does when it's only played a little differently
// (a placebo: a press shifted, dropped or added), so a variant is measured against every
// way normal play can go, not against one control. A different path through normal play
// stays inside the range; a broken game doesn't. Plan: docs/corruption/2026-11-06-placebo-plan.md.
namespace corrupt
{

constexpr uint32_t kSampleEvery = 10; // WRAM is sampled every this many frames
constexpr size_t kOamSize = 544;      // the sprite table: 128 x 4 bytes, then 32 bytes of high bits

// What one run did beyond its pictures.
struct StateTrace
{
    std::vector<std::vector<uint8_t>> wram;       // every kSampleEvery frames
    std::vector<std::vector<uint8_t>> oam;        // every frame, kOamSize bytes
    std::unordered_map<uint32_t, uint32_t> calls; // function entry -> frames it was called in
};

// A placebo's buttons: `pads` with one change somewhere in the first quarter (after the first
// twentieth): a press-change shifted 1-4 frames either way, a held press dropped, or a short
// press added. Never Start or Select. The same seed makes the same change; `what` says it.
std::vector<Pads> perturb(const std::vector<Pads> &pads, uint64_t seed, std::string *what = nullptr);

// Sprites teleporting: of the frames after the first, the share of sprite moves where a
// visible sprite's nearest visible sprite with the same tile in the frame before is over
// `far` pixels away. A sprite whose tile wasn't on screen the frame before (appearing,
// animating) doesn't count.
double sprite_jump_rate(const std::vector<std::vector<uint8_t>> &oam, int far = 48);

struct Signals
{
    // Looks different
    double apart = 0;       // the share differing over the second half against the nearest natural run
    double new_colours = 0; // the share of the second half's pixels in colours no natural run showed
    // Acts different
    uint32_t steady_broken = 0; // read bytes steady in normal play (<= 4 values) taking a value never seen
    uint32_t out_of_range = 0;  // read bytes that vary in normal play going a quarter of their range beyond it
    uint32_t new_functions = 0; // functions no natural run called
    uint32_t silenced = 0;      // functions every natural run called often, now called much less
    double sprite_jumps = 0;    // sprite_jump_rate
};

class Envelope
{
  public:
    static constexpr int kSteadyValues = 4;
    static constexpr int kThumbEvery = 4; // natural runs keep every 4th thumbnail of their second half

    // A natural run: what normal play did.
    void add(const RunRecord &run, const StateTrace &trace);
    // After the last natural run: `wram_reads` per byte (the bytes with reads are the ones the
    // game reads; empty: all of them).
    void finish(std::span<const uint32_t> wram_reads);
    size_t runs() const { return pictures.size(); }

    // A run against normal play. `own`: what the run's own rules write (WRAM ranges in it are
    // left out of the byte signals).
    Signals measure(const RunRecord &run, const StateTrace &trace, std::span<const AddressRange> own = {}) const;

  private:
    std::vector<std::vector<Picture>> pictures; // per natural run, its second half's kept thumbnails
    size_t frames = 0;                          // the runs' length (the shortest)
    std::bitset<4096> colours;
    struct Byte
    {
        uint64_t seen[4] = {};
        uint8_t lo = 255, hi = 0;
        bool read = true;
        bool steady = false;
    };
    std::vector<Byte> bytes;
    struct Calls
    {
        uint32_t min = 0, max = 0, in_runs = 0;
    };
    std::unordered_map<uint32_t, Calls> calls;
};

} // namespace corrupt
