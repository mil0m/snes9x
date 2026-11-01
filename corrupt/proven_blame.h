#pragma once
#include "crash_guard.h"
#include "rom_blame.h"
#include "test_ahead.h"

#include <span>

// Proven blame (roadmap #56): for a crash the guard can replay, show it again from a
// checkpoint, then without the changed bytes the game used since, then narrow those down;
// only what the replays prove is fixed. Plan: docs/corruption/2026-11-01-safety-plan.md.
namespace corrupt
{

struct ProofConfig
{
    unsigned max_runs = 10;      // replays per crash, the first included
    uint64_t max_frames = 4000;  // ...and their frames in all
    uint32_t tail = 30;          // frames played past the frame the crash was noticed in
};
// A proven set this small is fixed whole when narrowing runs out of budget.
constexpr size_t kFixAllAtMost = 8;

class ProvenBlame : public BlameProver
{
  public:
    ProvenBlame(RomBlame &blame, RomKeeper &rom) : blame(blame), rom(rom) {}

    void set_config(const ProofConfig &c) { cfg = c; }
    // The code map the tracer writes into (the runs put it back), and what to do around them.
    void set_map(std::span<uint8_t> f) { flags = f; }
    void set_hooks(const AheadHooks &h) { hooks = h; }
    // The guard's detector settings; frozen and black screens are left out (they need presses).
    void set_detector(const DetectorConfig &c);

    std::optional<Proof> prove(Engine &engine, CoreAccess &core, const CrashVerdict &verdict,
                               const std::vector<ProofStart> &starts) override;

    unsigned tried() const { return tries; }    // crashes it replayed
    unsigned proved() const { return proofs; }  // ...and found the bytes for
    // Why the others weren't: no changed byte used before the crash, the crash didn't come
    // back in the replay, the clean bytes didn't prevent it, or out of budget first.
    unsigned no_candidates = 0, not_reproduced = 0, unexplained = 0, over_budget = 0;
    unsigned unseen_culprits = 0; // proofs whose bytes the tracer never saw used
    void reset_counts() { tries = proofs = no_candidates = not_reproduced = unexplained = over_budget = unseen_culprits = 0; }

  private:
    RomBlame &blame;
    RomKeeper &rom;
    ProofConfig cfg;
    std::span<uint8_t> flags;
    AheadHooks hooks;
    DetectorConfig detect;
    RunAhead runner;
    unsigned tries = 0, proofs = 0;
};

} // namespace corrupt
