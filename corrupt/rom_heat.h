#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// ROM live heat (roadmap #50): what the game runs and reads in the ROM right now, fading
// over a second and a half, for the memory viewer's Live overlay. It reads the code map's
// touched bit (set by the tracer and the core's read hooks on every use) and clears it.
// Plan: docs/corruption/2026-11-01-safety-plan.md.
namespace corrupt
{

class RomHeat
{
  public:
    static constexpr uint32_t kFadeFrames = 90;

    void reset(size_t size); // a game was loaded: nothing glows
    // The viewer stopped showing the ROM: the next scan only clears what was touched meanwhile.
    void pause() { primed = false; }

    // Once a frame, between frames, after the crash blame has collected (it reads the same
    // bits for changed bytes): stamps every touched byte and clears its touched bit.
    // `also_used`: changed bytes the blame saw used this frame (it cleared their bits).
    void observe(uint8_t *flags, size_t size, std::span<const uint32_t> also_used = {});

    uint8_t level(size_t offset) const; // 255 when used this frame, down to 0 after the fade
    std::vector<uint8_t> levels() const;

  private:
    std::vector<uint32_t> used_at; // the frame each byte was last used; 0 never
    uint32_t frame = 0;
    bool primed = false;
};

} // namespace corrupt
