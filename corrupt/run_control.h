#pragma once
#include "crash_guard.h"
#include "engine.h"
#include "rom_keeper.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace corrupt
{

// Starts runs at a frame boundary. With the engine holding its starts
// (Engine::hold_starts), every (re)start waits until between_frames(), which
// snapshots the state there, or loads a saved one, and then lets frame 1 run.
// That snapshot is the run's anchor: loading it and starting the same recipe
// replays the same corruption. Call it between frames, before the crash guard.
//
// With a RomKeeper, a start also sets up the ROM: back to the clean ROM, then the
// recipe's at_load ROM rules, then (when there were any, and no saved moment is
// loaded) a power cycle so the game boots from that ROM. The anchor is taken after
// that, and the at_load rules on other targets fire last, so a replay from the anchor
// gets the same ROM and the same RAM.
class RunControl
{
  public:
    enum class Started
    {
        Nothing,      // no start was waiting
        Fresh,        // started here; the anchor is the current state
        FromAnchor,   // loaded the requested state and started from it
        AnchorFailed, // the requested state didn't load; started here instead
    };

    // The next start loads this state first. Used once.
    void request_start_from(std::vector<uint8_t> state) { pending = std::move(state); }
    void cancel_start_from() { pending.reset(); }
    bool start_from_pending() const { return pending.has_value(); }

    // A new recipe: the last run's moment and any requested one no longer apply.
    void forget_anchor()
    {
        current.clear();
        pending.reset();
        from_saved = false;
    }

    Started between_frames(Engine &engine, CoreAccess &core, RomKeeper *rom = nullptr);

    bool has_anchor() const { return !current.empty(); }
    const std::vector<uint8_t> &anchor() const { return current; }
    bool anchor_is_from_saved() const { return from_saved; } // the run began from a requested state
    Engine::PinsApplied last_pins() const { return pins; }     // what the last start did with the pins

  private:
    std::optional<std::vector<uint8_t>> pending;
    std::vector<uint8_t> current;
    bool from_saved = false;
    Engine::PinsApplied pins;
};

} // namespace corrupt
