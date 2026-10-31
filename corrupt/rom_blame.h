#pragma once
#include "crash_guard.h"
#include "rom_keeper.h"

#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace corrupt
{

// How one changed ROM byte has been used since it changed. Frames are the engine's.
struct ByteUse
{
    static constexpr uint64_t kNever = UINT64_MAX;
    uint64_t changed_at = 0;
    uint64_t first_used = kNever;
    uint64_t last_used = 0;
    uint32_t frames_used = 0; // frames in which the game ran or read it
    bool survived = false;    // already credited as a survival
};

// Blame by execution: a changed ROM byte can only cause a crash once the game runs it or
// reads it. Watches the bytes that differ from the clean ROM (the RomKeeper says when
// that starts and stops) and, between frames, stamps those whose touched bit the tracer or
// the core set in the code map. A crash then blames the changed bytes used in its window.
// Lives on the emulation thread, next to the RomKeeper it subscribes to.
class RomBlame : public ByteBlame
{
  public:
    static constexpr uint64_t kSurviveFrames = 900; // used for 15 s without a crash

    explicit RomBlame(RomKeeper &rom);
    ~RomBlame() override;

    // The code map the tracer records into (its touched bits are cleared here). Null: no
    // uses are seen, so nothing is blamed and the guard falls back to the rules alone.
    void attach(uint8_t *flags, size_t size);
    bool active() const { return flags != nullptr; }
    void set_policy(RomPolicy p, uint64_t seed);
    RomPolicy policy() const { return pol; }

    void collect(uint64_t frame); // after every frame, before the guard
    std::vector<uint32_t> take_survivals(); // bytes that turned survivors since the last take

    size_t suspects(uint64_t after, uint64_t through, bool only_new = false) const override;
    ByteBlameResult act(uint64_t after, uint64_t through, unsigned attempt, bool only_new = false) override;

    size_t watched() const { return offsets.size(); } // changed bytes
    const std::vector<uint32_t> &changed_offsets() const { return offsets; } // in no order
    size_t used() const;                               // changed bytes the game has used
    const ByteUse *use(uint32_t offset) const;

  private:
    void changed(uint32_t offset, bool now_changed);
    bool in_window(const ByteUse &u, uint64_t after, uint64_t through, bool only_new) const
    {
        return u.frames_used && u.first_used <= through && u.last_used > after && (!only_new || u.first_used > after);
    }

    RomKeeper &rom;
    uint8_t *flags = nullptr;
    size_t flags_size = 0;
    RomPolicy pol = RomPolicy::Revert;
    uint64_t seed = 0;
    uint64_t rerolls = 0;
    uint64_t frame = 0; // the last collected
    std::vector<uint32_t> offsets; // watched, with their uses alongside
    std::vector<ByteUse> uses;
    std::unordered_map<uint32_t, size_t> index;
    std::vector<uint32_t> survivals;
};

} // namespace corrupt
