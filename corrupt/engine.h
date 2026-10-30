#pragma once
#include "recipe.h"

#include <deque>
#include <string>
#include <vector>

namespace corrupt
{

struct LoggedEvent
{
    uint64_t frame;
    uint32_t rule_index;
    WriteEvent write;
};

// The run state a rollback restores. Decisions (salts, mutes, intensity) are
// deliberately not part of it: they are what a crash policy changes.
struct EngineSnapshot
{
    uint64_t frame = 0;
    std::vector<uint64_t> rule_ids; // which rule each runtime belongs to
    std::vector<RuleRuntime> runtimes;
};

struct EngineStatus
{
    bool active = false;
    uint64_t frame = 0;   // frames since activation
    uint64_t firings = 0; // across all rules since activation
    uint64_t seed = 0;
    std::string recipe_name;
};

// Runs a recipe, one frame at a time. Lives on the emulation thread only; the
// front end reads status() through its own thread hop.
class Engine
{
  public:
    static constexpr size_t kEventLogLimit = 10000;

    void set_recipe(Recipe recipe); // restarts the run
    const Recipe &recipe() const { return current; }

    // Turning on (again) restarts the run from frame 0, which is what makes
    // "recipe + seed from activation" replayable.
    void set_active(bool on);
    bool active() const { return is_active; }
    uint64_t current_frame() const { return frame; }

    void reroll(uint64_t seed); // new seed, restarts the run
    void press_hotkey(int n);   // used on the next frame
    void set_rule_enabled(size_t index, bool enabled);
    void set_info(std::string name, std::string notes); // no restart
    void set_intensity(double intensity);               // the whole recipe, 0..1, live
    void set_anchor_state(std::string file);            // the saved moment's file name; no restart

    void on_frame(MemoryAccess &mem);

    // Held starts: with hold_starts(true), every (re)start waits at frame 0 until
    // release(), so the front end can snapshot the state (or load a saved one) at a
    // frame boundary first. Off by default.
    void hold_starts(bool on) { hold = on; }
    bool waiting() const { return is_active && hold && !released; }
    void release() { released = true; }

    // Live edits. They leave the run and the other rules alone: no restart.
    // Every rule has a run-time id (never saved); a replaced rule gets a new one,
    // so snapshots taken before the edit don't hand it the old rule's runtime.
    void replace_rule(size_t index, Rule rule);
    size_t add_rule(Rule rule); // returns its index
    void remove_rule(size_t index);
    uint64_t rule_id(size_t index) const;

    // Rollback support.
    EngineSnapshot snapshot() const;
    // Also drops log entries newer than the snapshot. Rules are matched by id:
    // a rule the snapshot doesn't know starts fresh.
    void restore(const EngineSnapshot &snap);
    void reroll_rule(size_t index, uint64_t salt);
    void mute_rule_until(size_t index, uint64_t frame); // the rule skips frames <= `frame`
    uint64_t muted_until(size_t index) const;
    void set_rule_intensity(size_t index, double intensity);

    EngineStatus status() const;
    const std::deque<LoggedEvent> &event_log() const { return log; }

  private:
    void restart();

    Recipe current;
    std::vector<RuleRuntime> runtimes;
    std::vector<uint64_t> ids;
    uint64_t next_id = 1;
    std::vector<uint64_t> mutes; // per rule: quiet through this frame
    std::deque<LoggedEvent> log;
    bool is_active = false;
    uint64_t frame = 0;
    uint16_t pending_hotkeys = 0;
    bool hold = false;
    bool released = false;
};

} // namespace corrupt
