#pragma once
#include "crash_detector.h"
#include "engine.h"
#include "policy.h"

#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace corrupt
{

// What the guard needs from the emulator. Only ever called between frames,
// where loading a state is safe.
struct CoreAccess
{
    virtual std::vector<uint8_t> save_state() = 0;
    // The same into a buffer kept from last time: a state saved every frame (the instant
    // revert) mustn't pay for a fresh 800 KB allocation each time. False and empty on failure.
    virtual bool save_state_into(std::vector<uint8_t> &state)
    {
        state = save_state();
        return !state.empty();
    }
    virtual bool load_state(const std::vector<uint8_t> &state) = 0;
    virtual FrameSignals sample() = 0;
    // A power cycle, so the game boots again (from a corrupted ROM, for classic runs).
    virtual void reset_console() {}
    // Frames nobody sees or hears, for testing a change before it's applied (#52): begin,
    // run some, end. Between begin and end nothing is drawn or played, and what the front
    // end counts per frame (the reset watch, the WRAM counters) is put back at the end.
    virtual void begin_unseen() {}
    virtual void run_unseen_frame() {}
    virtual void end_unseen() {}
    virtual ~CoreAccess() = default;
};

// One line of the crash journal (written as JSON lines by the front end).
struct JournalEntry
{
    enum Kind
    {
        Crash,
        Recovered, // a clean stretch followed the last rollback
        GaveUp,
        InstantRevert, // a changed ROM byte sent the CPU into non-code: fixed, the frame replayed
        FirstUse,      // a changed ROM byte crashed a run ahead from its first use: fixed, the frame replayed
    } kind = Crash;

    std::string recipe;
    uint64_t seed = 0;
    std::string policy;
    std::string signal;
    std::string text;
    uint64_t frame = 0;
    uint64_t since_frame = 0;
    uint64_t restored_frame = 0;
    uint64_t frames_lost = 0;
    std::vector<std::string> suspects;
    std::string action;
    unsigned attempt = 0; // crashes in a row, this one included
    std::vector<uint32_t> rom_blamed; // single bytes acted on (ByteBlame), most suspicious first
    std::string rom_action;
};

// A second source of suspects besides the rules' writes: single bytes the game used in
// the crash window (the ROM blame, corrupt/rom_blame.h). Called by the guard while it
// handles a crash, after the rollback.
struct ByteBlameResult
{
    std::vector<uint32_t> offsets; // what was acted on, most suspicious first
    std::string text;              // "reverted 2 ROM bytes"
};
struct ByteBlame
{
    // How many suspects frames (after, through] have. With only_new, just the bytes first
    // used inside the window: for a guess like a calm screen, a byte that has been running
    // fine all along isn't evidence of anything.
    virtual size_t suspects(uint64_t after, uint64_t through, bool only_new) const = 0;
    // Act on them; `attempt` is the crash's number in a row (1-based).
    virtual ByteBlameResult act(uint64_t after, uint64_t through, unsigned attempt, bool only_new) = 0;
    // Whether acting removes the bytes for good (put back clean), so every crash it acts on
    // is progress that can't repeat forever, and needn't count towards giving up.
    virtual bool fixes_for_good() const { return false; }
    virtual ~ByteBlame() = default;
};

struct GuardOutcome
{
    enum Kind
    {
        Nothing,
        RolledBack,
        GaveUp, // corruption was switched off after rolling back once more
    } kind = Nothing;

    CrashVerdict verdict{};
    std::vector<uint32_t> suspects;
    std::vector<std::string> suspect_names;
    ActionKind action = ActionKind::Reroll;
    uint64_t restored_frame = 0;
    uint64_t blame_after = 0;   // the writes blamed: frames (blame_after, blame_through]
    uint64_t blame_through = 0;
    std::vector<uint32_t> rom_blamed; // what the byte blame acted on
    std::string rom_action;           // "reverted 2 ROM bytes"
    std::string summary; // short text for an on-screen message
};

// Catches crashes and rolls back. Call between_frames() after every emulated
// frame (the engine itself runs inside the frame, from the hook).
class CrashGuard
{
  public:
    static constexpr uint32_t kCheckpointInterval = 60;  // frames
    static constexpr size_t kDefaultCheckpoints = 30;     // ~30 s of history
    static constexpr uint64_t kRollbackMargin = 30;       // go back this far before the trouble started
    static constexpr uint64_t kCleanFrames = 600;         // 10 s without a crash resets the attempt count
    static constexpr unsigned kMaxAttempts = 5; // crashes in a row that fix nothing for good
    // ...and in a row at all, even while each one puts some byte back for good.
    static constexpr unsigned kMaxCrashesInRow = 40;
    static constexpr uint64_t kManualLookback = 120;      // a hand-marked crash blames the last 2 s
    // Each crash in a row goes this much further back: damage is often written long
    // before the game trips over it, and every checkpoint after it carries it along.
    static constexpr uint64_t kStepBackFrames = 300;

    GuardOutcome between_frames(Engine &engine, CoreAccess &core);

    void set_policy(PolicyKind p) { policy = p; }
    // Single bytes to blame besides the rules (the ROM blame); null for none.
    void set_byte_blame(ByteBlame *b) { bytes = b; }
    PolicyKind get_policy() const { return policy; }
    void set_detector_config(const DetectorConfig &c) { detector.set_config(c); }
    const DetectorConfig &detector_config() const { return detector.get_config(); }
    void mark_crash() { manual_pending = true; }
    void reset(); // forget checkpoints and counts, e.g. for a new recipe

    size_t checkpoint_count() const { return ring.size(); }
    // Crashes per rule, in the engine's current rule order. Kept by rule id, so they
    // follow a rule through edits; a replaced rule starts at 0.
    std::vector<unsigned> rule_crashes(const Engine &engine) const;
    unsigned consecutive_crashes() const { return consecutive; }
    unsigned total_crashes() const { return total; }

    std::function<void(const JournalEntry &)> on_journal;

  private:
    struct Checkpoint
    {
        uint64_t frame;
        std::vector<uint8_t> state;
        EngineSnapshot engine;
    };

    GuardOutcome handle(const CrashVerdict &verdict, Engine &engine, CoreAccess &core);
    std::vector<uint32_t> suspects_between(const Engine &engine, uint64_t after, uint64_t through) const;
    void restore(const Checkpoint &cp, Engine &engine, CoreAccess &core);
    void journal(JournalEntry e, const Engine &engine);

    CrashDetector detector;
    ByteBlame *bytes = nullptr;
    PolicyKind policy = PolicyKind::Reroll;
    std::deque<Checkpoint> ring;
    unsigned &crashes_of(const Engine &engine, uint32_t rule_index);

    std::map<uint64_t, unsigned> crashes_per_rule; // by rule id
    std::optional<uint64_t> last_frame;
    std::optional<uint64_t> recovering_since; // frame we last rolled back to
    unsigned consecutive = 0;
    unsigned fruitless = 0; // of those, the ones nothing was fixed for good in
    unsigned total = 0;
    bool manual_pending = false;
};

} // namespace corrupt
