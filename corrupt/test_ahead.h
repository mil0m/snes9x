#pragma once
#include "crash_detector.h"
#include "crash_guard.h"
#include "derail.h"
#include "engine.h"
#include "rom_keeper.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Test before applying (roadmap #52): a live ROM change only reaches the game once it has
// been played a few seconds ahead, unseen, without crashing. Plan:
// docs/corruption/2026-11-01-safety-plan.md.
namespace corrupt
{

struct AheadConfig
{
    uint32_t frames = 180; // how far ahead each test plays (3 s)
    unsigned tries = 3;    // picks tried per firing (the first, then re-rolls) before skipping it
    uint32_t settle_frames = 60; // a run stops early once every written byte was used this long ago
};

struct AheadResult
{
    enum Kind
    {
        Nothing,  // no live ROM write next frame: nothing tested
        Kept,     // tested, and the first pick goes ahead
        Rerolled, // the first pick crashed; a re-rolled one survived and goes ahead
        Skipped,  // every pick crashed: the engine skips this firing
    } kind = Nothing;

    unsigned runs = 0;    // runs ahead made, control runs included
    unsigned rerolls = 0;
    std::vector<uint32_t> written; // ROM offsets the real frame will change
    std::vector<uint32_t> skipped; // ROM offsets vetoed
    std::vector<uint32_t> untried; // of `written`, those the game didn't use in the window: untested
    std::string failure;  // what the last failed pick did ("stopped the CPU (STP at $00:806B) 0.2 s in")
    std::string summary;  // in full; empty for Nothing and a plain Kept
    std::string headline; // in a few words, for the panel's message line
    double ms = 0;        // wall time spent
};

// What the run ahead needs from around it, besides the core.
struct AheadHooks
{
    // The instant revert's watch (#51), taken once per frame ahead; none: landings aren't seen.
    std::function<std::optional<DerailWatch::Note>()> take_derail;
    // Around each run ahead: for keeping learning that isn't real out of the session (the
    // tracer's jump log).
    std::function<void()> begin, end;
};

// One run ahead, unseen and unheard, and everything put back after it: the core's state, the
// engine (and its pressed hotkeys), the ROM, the code map's flags. Used by test before
// applying (#52).
struct RunSpec
{
    // The bytes under test: their uses are seen, and a landing naming one of them is theirs.
    std::vector<uint32_t> watched;
    // The engine's ROM writes after this frame are dropped during the run (unset: none).
    std::optional<uint64_t> quiet_after;
    uint32_t frames = 180;
    uint32_t settle_frames = 60; // stops early once every watched byte was used this long ago
};

struct RunVerdict
{
    enum Kind
    {
        Survived,
        Crashed,
        Inconclusive, // a landing blamed on other changes: not these bytes' to answer for
    } kind = Survived;
    bool guilty = false; // crashed, and a watched byte was what did it beyond doubt (a landing)
    bool used = false;   // the game ran or read a watched byte
    std::vector<uint32_t> culprits; // with a landing: the watched bytes it blamed
    std::vector<uint32_t> unused;   // watched bytes the game never ran or read in the window
    uint32_t frames = 0;            // frames played
    std::string text;
};

class RunAhead
{
  public:
    RunVerdict play(Engine &engine, RomKeeper &rom, CoreAccess &core, std::span<uint8_t> flags,
                    const AheadHooks &hooks, const RunSpec &spec);

  private:
    CrashDetector detector;
    std::vector<uint8_t> state, flags_before;
};

class TestAhead
{
  public:
    void set_config(const AheadConfig &c) { cfg = c; }
    const AheadConfig &config() const { return cfg; }

    // Last thing between frames, before the next frame runs. If the engine's next frame
    // writes to the ROM, plays ahead with those writes and decides; the engine, the core,
    // the ROM and `flags` (the code map the tracer writes into) are left as they were, except
    // for a re-rolled rule's salt or a veto.
    AheadResult before_frame(Engine &engine, RomKeeper &rom, CoreAccess &core, std::span<uint8_t> flags,
                             const AheadHooks &hooks = {});

    unsigned tested() const { return firings_tested; } // firings tested since reset
    unsigned rerolled() const { return firings_rerolled; }
    unsigned skipped() const { return firings_skipped; }
    void reset_counts() { firings_tested = firings_rerolled = firings_skipped = 0; }

  private:
    AheadConfig cfg;
    RunAhead runner;
    unsigned firings_tested = 0, firings_rerolled = 0, firings_skipped = 0;
};

} // namespace corrupt
