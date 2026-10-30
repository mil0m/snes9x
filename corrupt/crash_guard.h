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
    virtual bool load_state(const std::vector<uint8_t> &state) = 0;
    virtual FrameSignals sample() = 0;
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
    static constexpr unsigned kMaxAttempts = 5;
    static constexpr uint64_t kManualLookback = 120;      // a hand-marked crash blames the last 2 s
    // Each crash in a row goes this much further back: damage is often written long
    // before the game trips over it, and every checkpoint after it carries it along.
    static constexpr uint64_t kStepBackFrames = 300;

    GuardOutcome between_frames(Engine &engine, CoreAccess &core);

    void set_policy(PolicyKind p) { policy = p; }
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
    PolicyKind policy = PolicyKind::Reroll;
    std::deque<Checkpoint> ring;
    unsigned &crashes_of(const Engine &engine, uint32_t rule_index);

    std::map<uint64_t, unsigned> crashes_per_rule; // by rule id
    std::optional<uint64_t> last_frame;
    std::optional<uint64_t> recovering_since; // frame we last rolled back to
    unsigned consecutive = 0;
    unsigned total = 0;
    bool manual_pending = false;
};

} // namespace corrupt
