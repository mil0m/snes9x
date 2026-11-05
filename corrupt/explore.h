#pragma once
#include "crash_detector.h"
#include "crash_guard.h"
#include "engine.h"
#include "moment.h"
#include "picture.h"
#include "recipe.h"
#include "rom_keeper.h"
#include "try_change.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The headless explorer (#45): from a kept moment, play many variants unseen, each a
// recipe, pressing the moment's own buttons, and score each against the moment played
// without corruption. Plan: docs/corruption/2026-11-05-explorer-plan.md.
namespace corrupt
{

enum class Family
{
    Reroll, // the moment's recipe with another seed
    Nudge,  // the moment's recipe with one rule changed
    Ram,    // one rule on WRAM, VRAM, OAM or CGRAM
    Code,   // one code rule (opcode swap, branch flip, NOP-out) in busy functions
    Swap,   // one pointer swap
    Kill,   // one busy function turned off
    Data,   // ROM bytes the game reads as data, changed slowly
};
constexpr size_t kFamilies = 7;
const char *family_name(Family f);

// What the generator may build on.
struct ExploreKnows
{
    std::optional<Recipe> recipe; // the moment's, for reroll and nudge
    bool code_map = false;        // code, data
    bool busy_code = false;       // code (aimed inside busy functions)
    bool jump_tables = false;     // swap
    bool functions = false;       // kill
    std::vector<AddressRange> wram; // WRAM ranges the game uses most; empty: anywhere in it
};

struct Variant
{
    uint32_t id = 0;
    Family family = Family::Ram;
    Recipe recipe;
    std::string what; // "wram $1200-$12FF: add -3..3 x2, every 10 frames"
};

// The WRAM the game reads most, for the ram family to aim at: the `blocks` most-read blocks
// of `block` bytes (merged where they touch), leaving out the direct page and stack areas
// ($0000-$01FF, $1E00-$1FFF), where a change mostly crashes. `reads`: per byte (#23's counters).
std::vector<AddressRange> busy_wram(std::span<const uint32_t> reads, size_t blocks = 32, uint32_t block = 256);

// `count` variants in equal shares of the families the knowledge allows, each with its own
// seed (from `seed`). The window is how long each plays, for timing one-off firings.
std::vector<Variant> make_variants(const ExploreKnows &knows, const MemoryAccess &mem, size_t count,
                                   uint64_t seed, uint32_t window);

// Where every variant starts: a moment's keyframe and ROM, and the buttons it pressed.
struct ExploreStart
{
    std::vector<uint8_t> state;
    std::vector<std::pair<uint32_t, uint8_t>> rom;
    std::vector<Pads> pads; // one per frame: the window
};
ExploreStart start_from(const Moment &m, uint32_t max_frames);

// One run: a thumbnail of every frame, and how it ended.
struct RunRecord
{
    std::vector<Picture> thumbs;
    std::vector<uint64_t> hashes;
    std::vector<Picture> kept; // full pictures of the frames asked for, in order
    bool crashed = false;
    std::string ended; // the crash, in words
    uint32_t frames = 0;
    uint32_t new_code = 0; // ROM bytes run as code that the map had never seen run
    uint64_t firings = 0;  // the recipe's rules fired this often
    size_t writes = 0;     // and wrote this many bytes (the engine's log, at most its limit)
};

struct ScoreConfig
{
    int threshold = 48;           // a pixel differs clearly (R + G + B)
    uint32_t motion_window = 120; // frames at the end whose picture changes count
    uint32_t detail_window = 60;  // frames at the end whose detail counts
    FreezeConfig freeze;
};

struct RunScore
{
    bool alive = false;
    bool crashed = false, froze = false;
    double different = 0; // 0..1
    double motion = 0, detail = 0, lively = 0; // 0..1; detail: looks like a game; lively: motion x detail
    // The raw numbers behind them, for measuring the score (#45 step 1): colours and edges at the
    // end against the control's (uncapped: garbage has more edges), and how different the last
    // second is.
    double colours_ratio = 0, edges_ratio = 0, end_different = 0;
    uint32_t new_code = 0;
    double score = 0;
};

// alive x visible x lively x (1 + 0.25 log2(1 + new code / 64)), where visible is the share
// that differs up to 10%, and lively is motion x looking like a game (edges and colours like
// the scene's, not garbage or blank). Tuned on blind ratings: see the plan's How it went.
RunScore score_run(const RunRecord &run, const RunRecord &control, const ScoreConfig &cfg = {});

// The crashes a run ends on: the definite ones (the CPU stopped, a deadlock, a runaway, a
// reboot). The guesses (black, frozen, NMI off) misread unseen frames, as #52 found; a freeze
// is told from the pictures instead (froze_at), and a black screen scores low as not lively.
inline DetectorConfig definite_crashes()
{
    DetectorConfig c;
    c.nmi_off_frames = 0;
    c.frozen_frames = 0;
    c.black_frames = 0;
    return c;
}

// What a run needs: the core, the ROM keeper the engine writes through, the engine (its
// frame hook is the host's: it must call engine.on_frame while the engine is active) and the
// code map's flags the tracer writes into (put back after each run).
struct ExploreRig
{
    CoreAccess &core;
    RomKeeper &rom;
    Engine &engine;
    std::span<uint8_t> flags;
    DetectorConfig detector = definite_crashes();
};

struct PlayOptions
{
    std::vector<uint32_t> keep; // frames whose full picture to keep
    // Keep what the tracer learned (the control: what the game runs anyway isn't new).
    bool keep_learning = false;
    // Right before each frame (after its buttons are set): the host's recorder, say.
    std::function<void(uint32_t frame)> before_frame;
    // After the last frame, before anything is put back: take the recorder's moment here.
    std::function<void()> after_frames;
};

// Plays `recipe` (none: the control) from `start`, unseen and drawn; everything is put back
// after: the core's state, the ROM, the flags, the engine off.
RunRecord play_variant(ExploreRig &rig, const ExploreStart &start, const Recipe *recipe,
                       const PlayOptions &options = {});

} // namespace corrupt
