#include "engine.h"

namespace corrupt
{

void Engine::set_recipe(Recipe recipe)
{
    current = std::move(recipe);
    restart();
}

void Engine::set_active(bool on)
{
    if (on && !is_active)
        restart();
    is_active = on;
    pending_hotkeys = 0;
}

void Engine::reroll(uint64_t seed)
{
    current.seed = seed;
    restart();
}

void Engine::press_hotkey(int n)
{
    if (is_active && n >= 1 && n <= 9)
        pending_hotkeys |= hotkey_bit(n);
}

void Engine::set_rule_enabled(size_t index, bool enabled)
{
    if (index < current.rules.size())
        current.rules[index].enabled = enabled;
}

void Engine::restart()
{
    runtimes.assign(current.rules.size(), RuleRuntime{});
    log.clear();
    frame = 0;
    pending_hotkeys = 0;
}

void Engine::on_frame(MemoryAccess &mem)
{
    if (!is_active)
        return;

    frame++;
    uint16_t hotkeys = pending_hotkeys;
    pending_hotkeys = 0;

    for (uint32_t i = 0; i < current.rules.size(); i++)
    {
        auto events = tick_rule(current.rules[i], runtimes[i], mem, current.seed, i, frame, hotkeys);
        for (auto &e : events)
            log.push_back({ frame, i, e });
    }

    while (log.size() > kEventLogLimit)
        log.pop_front();
}

EngineStatus Engine::status() const
{
    EngineStatus s;
    s.active = is_active;
    s.frame = frame;
    s.seed = current.seed;
    s.recipe_name = current.name;
    for (auto &rt : runtimes)
        s.firings += rt.firings;
    return s;
}

} // namespace corrupt
