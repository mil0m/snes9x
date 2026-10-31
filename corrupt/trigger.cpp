#include "trigger.h"

namespace corrupt
{

bool should_fire(const Trigger &t, uint64_t frame, Rng &rng, uint16_t pressed_hotkeys)
{
    if (t.type == TriggerType::AtLoad)
        return frame == 0; // the run's start; windows don't apply
    if (t.window && (frame < t.window->start || frame > t.window->end))
        return false;

    switch (t.type)
    {
    case TriggerType::OnceAtFrame: return frame == t.frame;
    case TriggerType::EveryFrames: return t.every > 0 && frame % t.every == 0;
    case TriggerType::ChancePerFrame: return rng.chance(t.chance);
    case TriggerType::Hotkey: return (pressed_hotkeys & hotkey_bit(t.hotkey)) != 0;
    case TriggerType::AtLoad: return false;
    }
    return false;
}

std::optional<std::string> validate_trigger(const Trigger &t)
{
    switch (t.type)
    {
    case TriggerType::OnceAtFrame:
        if (t.frame < 1)
            return "once_at_frame must be 1 or later (frame 1 is the first after activation)";
        break;
    case TriggerType::EveryFrames:
        if (t.every < 1)
            return "every_frames must be at least 1";
        break;
    case TriggerType::ChancePerFrame:
        if (t.chance < 0.0 || t.chance > 1.0)
            return "chance_per_frame must be within 0..1";
        break;
    case TriggerType::Hotkey:
        if (t.hotkey < 1 || t.hotkey > 9)
            return "hotkey must be 1..9";
        break;
    case TriggerType::AtLoad: break;
    }
    if (t.window && t.window->start > t.window->end)
        return "window start is after its end";
    return std::nullopt;
}

const char *trigger_name(TriggerType t)
{
    switch (t)
    {
    case TriggerType::OnceAtFrame: return "once_at_frame";
    case TriggerType::EveryFrames: return "every_frames";
    case TriggerType::ChancePerFrame: return "chance_per_frame";
    case TriggerType::Hotkey: return "hotkey";
    case TriggerType::AtLoad: return "at_load";
    }
    return "?";
}

std::optional<TriggerType> parse_trigger(std::string_view name)
{
    for (auto t : { TriggerType::OnceAtFrame, TriggerType::EveryFrames,
                    TriggerType::ChancePerFrame, TriggerType::Hotkey, TriggerType::AtLoad })
        if (name == trigger_name(t))
            return t;
    return std::nullopt;
}

} // namespace corrupt
