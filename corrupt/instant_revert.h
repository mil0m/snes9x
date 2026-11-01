#pragma once
#include "crash_guard.h"
#include "derail.h"
#include "engine.h"
#include "rom_blame.h"

#include <optional>
#include <string>
#include <vector>

namespace corrupt
{

struct InstantRevertResult
{
    Landing kind = Landing::None;
    uint32_t pc = 0;               // where the CPU landed
    uint64_t frame = 0;            // the frame that was replayed
    std::vector<uint32_t> offsets; // the ROM bytes fixed, newest first
    std::string rom_action;        // "reverted 1 ROM byte"
    std::string summary;           // for the status line
};

// The crash journal's line for it ("event": "instant_revert").
JournalEntry journal_entry(const InstantRevertResult &r);

// Instant revert (roadmap #51): when a changed ROM byte sends the CPU somewhere that can't
// be code (the tracer's DerailWatch notes it mid-frame), load the state the frame started
// from and fix the culprits for good, so the frame runs again without them. The engine is
// not rewound: its frame count goes on, so the crash guard sees time move forward as
// always. Lives on the emulation thread; only called between frames.
// Plan: docs/corruption/2026-11-01-safety-plan.md.
class InstantRevert
{
  public:
    static constexpr unsigned kMaxInRow = 8; // then the guard deals with it, until a clean frame

    void set_enabled(bool on)
    {
        enabled = on;
        if (!on)
            drop();
    }
    bool is_enabled() const { return enabled; }

    // First thing after a frame ran (`frame`: the engine's count now), with the tracer's note
    // if it made one. Nothing happens without a checkpoint from the frame just before (kept
    // by the last keep, at the count before this one or at this one: STP stops the count).
    std::optional<InstantRevertResult> after_frame(std::optional<DerailWatch::Note> note, uint64_t frame,
                                                   CoreAccess &core, RomBlame &blame);
    // Last thing between frames: keeps the state the next frame starts from, while there is
    // anything to protect (changed ROM bytes, the tracer on, the engine running). Kept even
    // while the revert is off, for test at first use (#55), which replays from it too.
    // With `engine`, its snapshot is kept alongside (a replay that fires the frame's rules again).
    void keep(CoreAccess &core, uint64_t frame, bool protect, const Engine *engine = nullptr);
    // The state the frame that just ran started from (`frame`: the engine's count now), and
    // the engine's then if kept, or nulls if there is none: the same rule as after_frame's.
    struct Checkpoint
    {
        const std::vector<uint8_t> *state = nullptr;
        const EngineSnapshot *engine = nullptr;
    };
    Checkpoint checkpoint_for(uint64_t frame) const
    {
        if (!state_frame || frame < *state_frame || frame > *state_frame + 1)
            return {};
        return { &state, engine_snap ? &*engine_snap : nullptr };
    }
    void drop(); // the checkpoint no longer belongs to the frame that comes next

    bool has_checkpoint() const { return state_frame.has_value(); }
    std::optional<uint64_t> checkpoint_frame() const { return state_frame; }
    unsigned total() const { return count; }
    unsigned in_row() const { return row; }
    void reset_counts() { count = row = 0; }

  private:
    bool enabled = true;
    std::vector<uint8_t> state;
    std::optional<uint64_t> state_frame;
    std::optional<EngineSnapshot> engine_snap;
    unsigned row = 0;
    unsigned count = 0;
};

} // namespace corrupt
