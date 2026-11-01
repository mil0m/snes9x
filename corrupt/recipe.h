#pragma once
#include "crash_detector.h"
#include "patches.h"
#include "policy.h"
#include "rule.h"

#include <optional>
#include <string>
#include <vector>

namespace corrupt
{

struct GameId
{
    std::string title;
    std::string sha256; // lowercase hex of the ROM's SHA-256
    std::string crc32;  // 8 hex digits, informational
};

struct Recipe
{
    static constexpr int kFormat = 1;

    std::string name;
    std::string notes;
    GameId game;
    uint64_t seed = 0;
    double intensity = 1.0; // 0..1, multiplies every rule's intensity (the panel's Chaos slider)
    std::string anchor_state; // phase 3: save state the recipe starts from
    std::vector<Rule> rules;
    std::vector<Patch> patches; // painted or imported bytes, applied as a run starts
    std::vector<Pin> pins;      // kept ROM changes (survivors), applied as a run starts, before the rules

    // Unset means "use the session's choice" (default policy, default detection).
    std::optional<PolicyKind> crash_policy;
    std::optional<RomPolicy> rom_policy; // for changed ROM bytes blamed by use
    // The instant revert (#51), on when unset: a changed ROM byte that sends the CPU into
    // non-code is fixed at once and the frame played again. Off: the guard catches it later.
    std::optional<bool> instant_revert;
    // Test before applying (#52): how many seconds ahead each live ROM change is played,
    // unseen, before it reaches the game (re-rolled or skipped if it crashes). Unset: the
    // default (3); 0: off.
    std::optional<double> test_first;
    std::optional<DetectorConfig> crash_detection;
};

} // namespace corrupt
