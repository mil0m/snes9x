#pragma once
#include "rom_blame.h"
#include "test_ahead.h"

#include <span>
#include <string>
#include <unordered_map>
#include <vector>

// Test at first use (roadmap #55): when the game runs or reads a changed ROM byte for the
// first time, play a second ahead from the start of that frame with it and, if that
// crashes, without it; a byte that alone makes the difference is fixed (the ROM policy's
// way) and the frame played again. Plan: docs/corruption/2026-11-01-safety-plan.md.
namespace corrupt
{

struct FirstUseConfig
{
    uint32_t frames = 180;       // the longest a run plays; also how long a byte may wait in the queue
    uint32_t settle_frames = 60; // a run stops once every byte under test was used this long ago
    uint32_t gap = 30;           // frames from one test to the next (at most 2 a second)
    size_t batch = 16;           // bytes tested together
    unsigned max_runs = 8;       // runs per test, control and narrowing down included
    unsigned tries = 3;          // failed values of one byte, then it's reverted whatever the policy
    uint32_t nmi_off_frames = 0; // NMI off this long in a run counts as a crash (0: not counted)
};

struct FirstUseResult
{
    enum Kind
    {
        Nothing, // nothing due, or no checkpoint to test from
        Kept,    // tested, and every byte stays
        Fixed,   // some bytes crashed the game where their clean values didn't: fixed, frame replayed
    } kind = Nothing;

    unsigned runs = 0;
    uint64_t frame = 0;            // the frame played again (Fixed)
    std::vector<uint32_t> tested;  // the batch
    std::vector<uint32_t> fixed;   // what was fixed
    bool older_damage = false;     // the run without them crashed too: not theirs
    std::string failure;           // what the first crashing run saw ("CPU stopped 0.3 s in")
    std::string rom_action;        // "reverted 1 ROM byte"
    std::string summary;           // for the status line; empty unless Fixed
    double ms = 0;
};

// The crash journal's line for it ("event": "first_use").
JournalEntry journal_entry(const FirstUseResult &r);

class FirstUseTest
{
  public:
    void set_config(const FirstUseConfig &c) { cfg = c; }
    const FirstUseConfig &config() const { return cfg; }

    // Right after RomBlame::collect(frame): queues the changed bytes the game used for the
    // first time in the frame that just ran.
    void note_uses(const RomBlame &blame, const RomKeeper &rom, uint64_t frame);
    // Test before applying (#52) saw these bytes used in its window: they've had a test.
    void vouch(const std::vector<uint32_t> &offsets, uint64_t frame);

    // Between frames, after note_uses, with the state the frame that just ran started from
    // (and the engine's snapshot from then, so the runs fire that frame's rules again). If a
    // test is due, plays the oldest queued bytes ahead from there; on a Fixed result the core
    // is back at `checkpoint` (the frame plays again; the engine isn't rewound, as with the
    // instant revert) and the bytes are fixed. Otherwise the core, engine, ROM and `flags`
    // are as they were.
    FirstUseResult test(Engine &engine, RomKeeper &rom, RomBlame &blame, CoreAccess &core, std::span<uint8_t> flags,
                        const AheadHooks &hooks, const std::vector<uint8_t> *checkpoint,
                        const EngineSnapshot *engine_checkpoint = nullptr);

    void forget(); // a new run or game: nothing queued, tested or vouched for
    size_t queued() const { return queue.size(); }
    // Whether a byte was tested (and passed) at its current value, or #52 vouched for it.
    bool passed(uint32_t offset) const { return tested_value.count(offset) > 0; }
    // The frame a byte was last tested in (diagnostics), or none.
    std::optional<uint64_t> tested_at(uint32_t offset) const
    {
        auto it = test_frame.find(offset);
        return it == test_frame.end() ? std::nullopt : std::optional<uint64_t>(it->second);
    }
    unsigned tests() const { return test_count; }      // batches tested since reset_counts
    unsigned bytes_tested() const { return byte_count; }
    unsigned fixes() const { return fix_count; }       // bytes fixed
    void reset_counts() { test_count = byte_count = fix_count = 0; }

  private:
    struct Queued
    {
        uint32_t offset;
        uint8_t value;  // the changed value first used
        uint64_t frame; // first used in
    };

    FirstUseConfig cfg;
    RunAhead runner;
    std::vector<Queued> queue;                          // oldest first
    std::unordered_map<uint32_t, uint8_t> tested_value; // offset -> the value that passed
    std::unordered_map<uint32_t, uint64_t> vouched;     // offset -> frame #52 used it in
    std::unordered_map<uint32_t, unsigned> failures;    // offset -> values that failed
    std::unordered_map<uint32_t, uint64_t> test_frame;  // offset -> the frame it was last tested in
    std::optional<uint64_t> last_test;
    unsigned test_count = 0, byte_count = 0, fix_count = 0;
};

} // namespace corrupt
