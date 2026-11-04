#pragma once
#include "patches.h"
#include "test_ahead.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

// Try a change before it lands (#78): play one change a few seconds ahead, unseen, and once
// without it, and say what happened. Plan: docs/corruption/2026-11-02-function-changes-plan.md.
// Measured (corrupt-smoke --foresee): at 5 s, a still picture where the run without the
// change kept moving caught 64 of 79 single changes that failed within 30 s, and called 4 of
// 610 survivors failed; definite crashes alone caught 10.
namespace corrupt
{

constexpr uint32_t kTryFrames = 300; // 5 s: measured, it sees 81% of failures; 3 s fewer, 10 s more false alarms

struct FreezeConfig
{
    uint32_t hold = 60;     // the window's last second holds one picture...
    uint32_t min_moves = 3; // ...while the run without the change changed picture this often in it
};

// The frame (0-based) a freeze began in `run`, judged at its end against `control` (the same
// moment without the change): its last `hold` pictures are one picture while the control's
// changed `min_moves` times or more in those frames. Nothing: not frozen, or can't tell.
std::optional<uint32_t> froze_at(std::span<const uint64_t> run, std::span<const uint64_t> control,
                                 const FreezeConfig &cfg = {});

// Whether the control moved enough at the end for a freeze to be told at all.
bool control_moves(std::span<const uint64_t> control, const FreezeConfig &cfg = {});

struct TryResult
{
    enum Kind
    {
        Running, // still running at the end (it may still look like the run without it)
        Froze,   // the picture stopped where without the change it kept moving
        Crashed, // a definite crash (CPU stopped, a landing in non-code...)
        Unused,  // the game didn't run or read the changed bytes in the window: no answer
        Unclear, // without the change it went wrong too, or something else did
        Failed,  // couldn't try (no saved state)
    } kind = Failed;
    uint32_t frames = 0;  // the window
    uint32_t at = 0;      // Froze/Crashed: the frame it began
    bool differs = false; // Running: the last picture isn't the one without the change
    bool still_anyway = false; // Running: the picture doesn't move without it either, so a freeze can't be told
    std::string text;     // a sentence for the editor
};

// The verdict from the two runs. `use_known`: the tracer marked what the game used, so
// "didn't run it" can be said.
TryResult judge_try(const RunVerdict &with, const RunVerdict &without, uint32_t frames, bool use_known,
                    const FreezeConfig &cfg = {});

// Both runs from exactly here: the change's bytes in (`c.value`) and out (`c.was`), queued as
// flips for the next frame; everything is put back after (core, engine and its queued flips,
// ROM, code map flags). The engine must be active (flips need it).
TryResult try_change(RunAhead &runner, Engine &engine, RomKeeper &rom, CoreAccess &core, std::span<uint8_t> flags,
                     const AheadHooks &hooks, const Change &c, uint32_t frames, bool use_known,
                     const FreezeConfig &cfg = {});

} // namespace corrupt
