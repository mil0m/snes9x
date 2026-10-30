#include "policy.h"

namespace corrupt
{

const char *policy_name(PolicyKind p)
{
    switch (p)
    {
    case PolicyKind::Reroll: return "reroll";
    case PolicyKind::Skip: return "skip";
    case PolicyKind::Dampen: return "dampen";
    case PolicyKind::Disable: return "disable";
    case PolicyKind::Escalate: return "escalate";
    }
    return "?";
}

std::optional<PolicyKind> parse_policy(std::string_view name)
{
    for (auto p : { PolicyKind::Reroll, PolicyKind::Skip, PolicyKind::Dampen, PolicyKind::Disable, PolicyKind::Escalate })
        if (name == policy_name(p))
            return p;
    return std::nullopt;
}

const char *action_name(ActionKind a)
{
    switch (a)
    {
    case ActionKind::Reroll: return "reroll";
    case ActionKind::Skip: return "skip";
    case ActionKind::Dampen: return "dampen";
    case ActionKind::Disable: return "disable";
    }
    return "?";
}

ActionKind escalation_action(unsigned crashes)
{
    if (crashes <= 2)
        return ActionKind::Reroll;
    if (crashes <= 4)
        return ActionKind::Dampen;
    return ActionKind::Disable;
}

ActionKind decide(PolicyKind policy, unsigned rule_crashes)
{
    switch (policy)
    {
    case PolicyKind::Reroll: return ActionKind::Reroll;
    case PolicyKind::Skip: return ActionKind::Skip;
    case PolicyKind::Dampen: return ActionKind::Dampen;
    case PolicyKind::Disable: return ActionKind::Disable;
    case PolicyKind::Escalate: return escalation_action(rule_crashes);
    }
    return ActionKind::Reroll;
}

} // namespace corrupt
