#pragma once
#include "crash_detector.h"
#include "engine.h"
#include "policy.h"

#include <deque>
#include <functional>
#include <map>
#include <optional>
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

// Proven blame (#56): before the guard acts on a crash it can replay, something that plays
// the stretch before it again, unseen, to find which changed bytes it needs. Starts are
// the guard's checkpoints, newest first; a proof names the one to roll back to.
struct ProofStart
{
    uint64_t frame = 0;
    const std::vector<uint8_t> *state = nullptr;
    const EngineSnapshot *engine = nullptr;
};
struct Proof
{
    size_t start = 0;         // index into the starts given
    ByteBlameResult fixed;    // what was fixed for good
    unsigned runs = 0;        // replays made
    uint64_t frames = 0;      // ...and their frames
    double ms = 0;
};
struct BlameProver
{
    // Nothing: it couldn't show the crash again, or prove anything; the guard guesses as before.
    virtual std::optional<Proof> prove(Engine &engine, CoreAccess &core, const CrashVerdict &verdict,
                                       const std::vector<ProofStart> &starts) = 0;
    virtual ~BlameProver() = default;
};
// Crashes a replay can show again without the player (frozen and black screens need presses).
bool replayable(CrashSignal s);

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
    bool proven = false;              // the bytes were found by replaying (#56)
    unsigned proof_runs = 0;
    double proof_ms = 0;
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
    // Proven blame (#56): checkpoints offered, each this much further back.
    static constexpr unsigned kProofStarts = 3;
    static constexpr uint64_t kProofStep = 300;
    // ...only for a crash that came back: the same kind, starting within this of the last.
    static constexpr uint64_t kSameCrashFrames = 120;

    GuardOutcome between_frames(Engine &engine, CoreAccess &core);

    void set_policy(PolicyKind p) { policy = p; }
    // Single bytes to blame besides the rules (the ROM blame); null for none.
    void set_byte_blame(ByteBlame *b) { bytes = b; }
    // Proven blame (#56); null: the guard only guesses.
    void set_prover(BlameProver *p) { prover = p; }
    // Replay every crash it can, not only one that came back (measurements, tests).
    void set_prove_first_crash(bool on) { prove_first = on; }
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
    GuardOutcome proven(const CrashVerdict &verdict, Engine &engine, CoreAccess &core, uint64_t target);
    std::vector<uint32_t> suspects_between(const Engine &engine, uint64_t after, uint64_t through) const;
    void restore(const Checkpoint &cp, Engine &engine, CoreAccess &core);
    void journal(JournalEntry e, const Engine &engine);

    CrashDetector detector;
    ByteBlame *bytes = nullptr;
    BlameProver *prover = nullptr;
    bool prove_first = false;
    PolicyKind policy = PolicyKind::Reroll;
    std::deque<Checkpoint> ring;
    unsigned &crashes_of(const Engine &engine, uint32_t rule_index);

    std::map<uint64_t, unsigned> crashes_per_rule; // by rule id
    std::optional<uint64_t> last_frame;
    std::optional<uint64_t> recovering_since; // frame we last rolled back to
    unsigned consecutive = 0;
    std::optional<CrashVerdict> last_verdict; // the crash before this one, in this row
    unsigned fruitless = 0; // of those, the ones nothing was fixed for good in
    unsigned total = 0;
    bool manual_pending = false;
};

} // namespace corrupt
