#pragma once
#include "recipe.h"

#include <deque>
#include <span>
#include <unordered_map>
#include <functional>
#include <optional>
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
    void rename_region(const std::string &from, const std::string &to); // empty `to`: unnamed; no restart
    void set_hot_mask(HotMask mask); // for rules with avoid_hot; no restart
    void set_locked(HotMask mask);   // bytes no rule may write (the ROM header and vectors); no restart
    // Where aimed ROM rules learn what each byte is: the code map's flags (everyone's).
    // Called once per run, when an aimed rule first fires; the span must stay valid until
    // the next run starts.
    void set_code_flags(std::function<std::span<const uint8_t>()> flags) { code_flags = std::move(flags); }
    // Where pointer swaps learn the game's jump tables (everyone's). Called when a jump op
    // first fires in a run, and again while it returns none.
    void set_jump_tables(std::function<std::vector<JumpTable>()> tables) { jump_tables = std::move(tables); }
    // How many bytes an aimed rule picks from this run, or entries a pointer swap may change
    // (0 until it first fires).
    size_t aimed_count(size_t index) const;

    void on_frame(MemoryAccess &mem);

    // The run's start, before frame 1: fires the at_load rules (those `which` accepts the
    // target of; all by default), each once per run, logged at frame 0. Returns the
    // number of bytes written.
    size_t fire_at_load(MemoryAccess &mem, const std::function<bool(Target)> &which = {});
    bool has_at_load(Target t) const; // an enabled at_load rule on that target

    // The recipe's patches (painted bytes): applied as the run starts, through `mem`,
    // skipping locked bytes and never logged as rule writes. Returns the bytes written.
    size_t apply_patches(MemoryAccess &mem, const std::function<bool(Target)> &which = {});
    bool has_patches(Target t) const;

    // The recipe's pins: applied as the run starts, through `mem`, before the rules, never
    // logged as rule writes. A pin whose `was` isn't the byte's value now (another version
    // of the ROM) is skipped. Call it right after the ROM is back to clean.
    struct PinsApplied
    {
        size_t written = 0;
        size_t mismatched = 0;
    };
    PinsApplied apply_pins(MemoryAccess &mem);
    bool has_pins() const { return !current.pins.empty(); }
    void add_pins(const std::vector<Pin> &pins);       // a pin on the same byte is replaced; no restart
    void remove_pins(const std::vector<uint32_t> &offsets); // no restart
    void add_patches(Target t, const std::vector<std::pair<uint32_t, uint8_t>> &bytes); // no restart
    void clear_patches(Target t);                                                          // no restart

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

    // Test before applying (#52). The writes the next frame will make (only those to `only`,
    // when given), worked out against memory that keeps none of them; the engine is left as
    // it was, pressed hotkeys included.
    std::vector<LoggedEvent> preview_next(MemoryAccess &mem, std::optional<Target> only = std::nullopt);
    // Hotkeys pressed for the next frame (a snapshot doesn't hold them; a run ahead must).
    uint16_t pressed_hotkeys() const { return pending_hotkeys; }
    void set_pressed_hotkeys(uint16_t bits) { pending_hotkeys = bits; }
    // Writes the engine skips (and doesn't log) whenever it runs `frame`: a change that
    // failed its test. Kept through restores and runs ahead until dropped.
    void veto(uint64_t frame, Target target, const std::vector<uint32_t> &offsets);
    void drop_vetoes_through(uint64_t frame);
    size_t vetoes() const { return vetoed.size(); }
    // While set, writes to `target` in frames after `frame` don't land (and aren't logged):
    // a run ahead tests one firing, not the ones after it.
    void quiet_after(std::optional<uint64_t> frame, Target target = Target::ROM)
    {
        quiet_from = frame;
        quiet_target = target;
    }

    EngineStatus status() const;
    const std::deque<LoggedEvent> &event_log() const { return log; }
    // Log entries a restore to an earlier snapshot dropped, put back after a run ahead from
    // there (#55). Appended as they are; the caller gives them in order.
    void put_back_log(const std::vector<LoggedEvent> &events)
    {
        log.insert(log.end(), events.begin(), events.end());
        while (log.size() > kEventLogLimit)
            log.pop_front();
    }

  private:
    void restart();
    const AimedBytes *aim_for(uint32_t index, MemoryAccess &mem);

    Recipe current;
    std::vector<RuleRuntime> runtimes;
    std::vector<uint64_t> ids;
    uint64_t next_id = 1;
    std::vector<uint64_t> mutes; // per rule: quiet through this frame
    std::deque<LoggedEvent> log;
    struct Veto
    {
        uint64_t frame;
        Target target;
        uint32_t offset;
    };
    std::vector<Veto> vetoed;
    std::optional<uint64_t> quiet_from;
    Target quiet_target = Target::ROM;
    HotMask hot, locked;
    HotMask hot_and_locked; // what avoiding rules skip
    bool is_active = false;
    uint64_t frame = 0;
    uint16_t pending_hotkeys = 0;
    bool hold = false;
    bool released = false;
    std::function<std::span<const uint8_t>()> code_flags;
    std::unordered_map<uint64_t, std::vector<uint32_t>> aimed; // by rule id, for this run
    AimedBytes aimed_now;
    std::span<const uint8_t> run_flags; // the code map this run aims with
    std::function<std::vector<JumpTable>()> jump_tables;
    std::vector<JumpTable> run_jumps;                             // the tables this run swaps with
    std::unordered_map<uint64_t, std::vector<SwapSlot>> swaps;    // by rule id, for this run
};

} // namespace corrupt
