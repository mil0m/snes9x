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

    void on_frame(MemoryAccess &mem);

    // Rollback support.
    EngineSnapshot snapshot() const;
    void restore(const EngineSnapshot &snap); // also drops log entries newer than the snapshot
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
    std::vector<uint64_t> mutes; // per rule: quiet through this frame
    std::deque<LoggedEvent> log;
    bool is_active = false;
    uint64_t frame = 0;
    uint16_t pending_hotkeys = 0;
};

} // namespace corrupt
