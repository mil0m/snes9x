#pragma once
#include "crash_guard.h"
#include "memory.h"
#include "recipe.h"

#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Keep a golden moment (#47): the game from a little before, and everything that happened
// since, so it plays back exactly. Outcomes, not causes: a keyframe (a saved state and the
// ROM's changed bytes) and, for each frame after it, the pads and every byte corruption
// wrote, before the frame and in its hook. Nothing that made those writes (the engine, the
// guard, Paint) has to replay. Plan: docs/corruption/2026-11-04-golden-moments-plan.md.
namespace corrupt
{

struct MomentWrite
{
    Target target = Target::WRAM;
    uint32_t offset = 0;
    uint8_t value = 0;
    bool operator==(const MomentWrite &) const = default;
};

struct MomentFrame
{
    Pads pads{};
    std::vector<MomentWrite> before; // written between the last frame and this one
    std::vector<MomentWrite> during; // written in the frame hook
};

struct Moment
{
    // For people.
    std::string name;
    GameId game;
    std::string recipe;  // the recipe as JSON, when there was one
    std::string recipe_name;
    uint64_t seed = 0;
    std::string made_by;
    std::string made_at; // "2026-11-04 21:13"
    std::string starts;  // why it starts where it does when that's later than usual ("the crash guard rolled back")
    // For playing it.
    std::vector<uint8_t> state;                    // the keyframe
    std::vector<std::pair<uint32_t, uint8_t>> rom; // ROM bytes that differed from the clean ROM then
    std::vector<MomentFrame> frames;
    std::vector<MomentWrite> after; // written after the last frame, before it was kept
    uint64_t from = 0; // the recorder's frame count at the keyframe (for diagnostics; not saved)

    double seconds() const { return frames.size() / 60.0; }
};

struct RecorderConfig
{
    uint32_t keyframe_every = 600; // 10 s
    size_t keyframes = 3;          // kept: 20-30 s of history
    uint32_t lead = 600;           // a moment starts at the newest keyframe at least this far back
};

// Records while a game runs. Lives on the emulation thread. Every corruption write reaches
// it through RecordedMemory; the front end calls before_frame() right before each frame the
// player sees, and hook_begin()/hook_end() around the frame hook.
class MomentRecorder
{
  public:
    void set_config(const RecorderConfig &c) { cfg = c; }
    const RecorderConfig &config() const { return cfg; }

    // Another game, or recording off: everything goes.
    void reset();

    // Right before a frame runs. `timeline` counts the core's state loads and resets: when it
    // moved without the recorder being told why, the game jumped elsewhere, and history starts
    // again here. Saves a keyframe when one is due (`clean`: the clean ROM, for its changes).
    void before_frame(CoreAccess &core, const MemoryAccess &mem, std::span<const uint8_t> clean, uint64_t timeline);
    void hook_begin() { in_hook = true; }
    void hook_end() { in_hook = false; }
    void wrote(Target t, uint32_t offset, uint8_t value);

    // A run ahead (#52, #78) or a check: its writes don't count, and when it's over the loads it
    // made put everything back, so `accept` the timeline as it is then.
    void hold(bool on) { held = on; }
    bool holding() const { return held; }
    void accept(uint64_t timeline) { seen = timeline; }
    // The game jumped elsewhere (a save state loaded): history starts again at the next frame.
    void cut(const std::string &why);
    // The frame that just ran is played again from its start (the instant revert, #51): it is
    // recorded again. Its ROM writes stay (save states don't hold the ROM), the rest is undone.
    void redo_frame(uint64_t timeline);
    // Between frames, right after the crash guard's turn (#79): where its checkpoint of engine
    // frame `frame`, if it took one, falls in the history. Marking a frame number again replaces
    // the older mark (the guard goes back to its newest checkpoint of a frame).
    void mark(uint64_t frame);
    // The guard rolled back to its checkpoint of `frame`: the history before it stays, the frames
    // since go (with any keyframe of theirs), and their ROM writes stand (save states don't hold
    // the ROM; the rollback's own ROM writes come after). False: the mark is gone or no keyframe
    // is left before it, and the history starts again, `why`.
    bool back_to(uint64_t frame, uint64_t timeline, const std::string &why);

    // The moment: from the newest keyframe at least `lead` frames back (or the oldest) to now.
    // Nothing before the first keyframe.
    std::optional<Moment> take() const;
    uint64_t frames_recorded() const { return frames.size(); } // since the oldest keyframe
    uint64_t frames_to_keep() const; // how many frames take() would give now
    const std::string &last_cut() const { return cut_why; }
    uint64_t frames_begun() const { return now; }

  private:
    struct Keyframe
    {
        uint64_t at = 0; // the frame it comes before
        std::vector<uint8_t> state;
        std::vector<std::pair<uint32_t, uint8_t>> rom;
    };
    struct Mark
    {
        uint64_t frame = 0;   // the engine's
        uint64_t at = 0;      // the recorder's frame that comes next
        size_t written = 0;   // writes waiting for it then
    };
    RecorderConfig cfg;
    std::deque<Keyframe> keys;
    std::deque<Mark> marks;
    std::deque<MomentFrame> frames; // frames[i] is frame first + i
    uint64_t first = 0, now = 0;    // now: frames begun
    std::vector<MomentWrite> pending; // before the next frame
    std::optional<uint64_t> seen;
    bool cut_due = true;
    bool in_hook = false, held = false;
    std::string cut_why, cut_due_why;
    uint64_t cut_at = 0; // the frame the history starts at because of the last cut
};

// Writes through `inner` and tells the recorder.
class RecordedMemory : public MemoryAccess
{
  public:
    RecordedMemory(MemoryAccess &inner, MomentRecorder &recorder) : inner(inner), recorder(recorder) {}
    size_t size(Target t) const override { return inner.size(t); }
    uint8_t read(Target t, uint32_t offset) const override { return inner.read(t, offset); }
    const uint8_t *data(Target t) const override { return inner.data(t); }
    void write(Target t, uint32_t offset, uint8_t value) override
    {
        inner.write(t, offset, value);
        recorder.wrote(t, offset, value);
    }

  private:
    MemoryAccess &inner;
    MomentRecorder &recorder;
};

// Plays a moment back. The front end calls before_frame() right before each frame and
// in_hook() from the frame hook.
class MomentPlayer
{
  public:
    // Loads the keyframe and puts the ROM as it was then (bytes the moment doesn't change go
    // back to `clean`). False: the state didn't load.
    bool start(Moment m, CoreAccess &core, MemoryAccess &mem, std::span<const uint8_t> clean);
    // Right before a frame: the pads and the writes before it. False when the moment is over
    // (its last writes are made then, and it stops).
    bool before_frame(CoreAccess &core, MemoryAccess &mem);
    void in_hook(MemoryAccess &mem);
    // Someone pressed or let go of a button since the last frame: they take over.
    bool taken_over(CoreAccess &core) const;
    void stop() { on = false; }

    bool playing() const { return on; }
    size_t frame() const { return next; } // frames played
    size_t frames() const { return moment.frames.size(); }
    const Moment &current() const { return moment; }

  private:
    Moment moment;
    size_t next = 0;
    std::optional<size_t> hook_frame; // the frame whose hook writes are due
    bool on = false;
    Pads set{};
};

struct MomentCheck
{
    bool exact = false;
    bool played = false; // false: it couldn't be played (the state didn't load)
    bool drawn = false;  // the answer is from a drawn playback
    std::vector<Target> differ;
    uint32_t frames = 0;
    double ms = 0;
    std::string text;
};

struct CheckHooks
{
    std::function<void()> begin, end; // around the playback (the tracer's jump log)
    std::span<uint8_t> flags;         // the code map's flags, put back after
    bool draw = false; // draw the frames: no difference in 252 moments (corrupt-smoke --keep), and slower
    // Not drawn and it drifts: play it again drawn (#86). A game can read what only drawing sets
    // (the sprite overflow flags mid-frame), and the game drew its frames, so drawn is the answer
    // then; in the usual case the check stays as fast as before.
    bool redraw = true;
};

// Plays the moment unseen from its keyframe and compares every memory with now; everything is
// put back after (the core, the ROM, the pads, the flags). `mem` must be the memory itself
// (not the ROM keeper or the recorder); `player` must be wired to the frame hook.
MomentCheck check_moment(const Moment &m, MomentPlayer &player, CoreAccess &core, MemoryAccess &mem,
                         std::span<const uint8_t> clean, const CheckHooks &hooks = {});

// The file (.s9xmoment): a JSON header, then the keyframe, the ROM bytes and the frames
// (packed with zlib where the build has it).
std::string encode_moment(const Moment &m);
std::optional<Moment> decode_moment(const std::string &bytes, std::string &error);

// What a list shows, read from the file's header alone.
struct MomentInfo
{
    std::filesystem::path path;
    std::string name, recipe_name, made_by, made_at, starts;
    double seconds = 0;
    GameId game;
};
std::optional<MomentInfo> read_moment_info(const std::filesystem::path &path, std::string &error);

} // namespace corrupt
