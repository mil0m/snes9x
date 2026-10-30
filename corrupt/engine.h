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

    void reroll(uint64_t seed); // new seed, restarts the run
    void press_hotkey(int n);   // used on the next frame
    void set_rule_enabled(size_t index, bool enabled);

    void on_frame(MemoryAccess &mem);

    EngineStatus status() const;
    const std::deque<LoggedEvent> &event_log() const { return log; }

  private:
    void restart();

    Recipe current;
    std::vector<RuleRuntime> runtimes;
    std::deque<LoggedEvent> log;
    bool is_active = false;
    uint64_t frame = 0;
    uint16_t pending_hotkeys = 0;
};

} // namespace corrupt
