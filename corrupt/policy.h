#pragma once
#include <optional>
#include <string_view>

namespace corrupt
{

// What to do about a crash. Kept small and swappable on purpose: which policy
// works best will differ per game and needs real-play iteration (see the journal).
enum class PolicyKind
{
    Reroll,   // replay with fresh random values for the suspect rules (default)
    Skip,     // replay without the suspects' firings up to the crash, then carry on
    Dampen,   // halve the suspects' intensity
    Disable,  // switch the suspects off
    Escalate, // re-roll twice, then dampen twice, then disable
};

enum class ActionKind
{
    Reroll,
    Skip,
    Dampen,
    Disable,
};

const char *policy_name(PolicyKind p);
std::optional<PolicyKind> parse_policy(std::string_view name);
const char *action_name(ActionKind a);

// What to do with single changed ROM bytes blamed for a crash (corrupt/rom_blame.h).
enum class RomPolicy
{
    Revert, // back to the clean value (default)
    Reroll, // a fresh random value
};
const char *rom_policy_name(RomPolicy p);
std::optional<RomPolicy> parse_rom_policy(std::string_view name);

// Escalate's ladder, by how many crashes this rule has been a suspect in (1-based).
ActionKind escalation_action(unsigned crashes);

// The action for one suspect rule.
ActionKind decide(PolicyKind policy, unsigned rule_crashes);

} // namespace corrupt
