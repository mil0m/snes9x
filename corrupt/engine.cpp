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
    mutes.assign(current.rules.size(), 0); // they refer to frames of the previous run
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
        if (frame <= mutes[i])
            continue;
        auto &rule = current.rules[i];
        auto events = tick_rule(rule, runtimes[i], mem, Rng::rule_seed(current.seed, rule.salt), i, frame, hotkeys);
        for (auto &e : events)
            log.push_back({ frame, i, e });
    }

    while (log.size() > kEventLogLimit)
        log.pop_front();
}

EngineSnapshot Engine::snapshot() const
{
    return { frame, runtimes };
}

void Engine::restore(const EngineSnapshot &snap)
{
    if (snap.runtimes.size() != current.rules.size())
        return; // taken under a different recipe
    frame = snap.frame;
    runtimes = snap.runtimes;
    pending_hotkeys = 0;
    while (!log.empty() && log.back().frame > frame)
        log.pop_back();
}

void Engine::reroll_rule(size_t index, uint64_t salt)
{
    if (index < current.rules.size())
        current.rules[index].salt = salt;
}

void Engine::mute_rule_until(size_t index, uint64_t until)
{
    if (index < mutes.size())
        mutes[index] = until;
}

uint64_t Engine::muted_until(size_t index) const
{
    return index < mutes.size() ? mutes[index] : 0;
}

void Engine::set_rule_intensity(size_t index, double intensity)
{
    if (index < current.rules.size())
        current.rules[index].intensity = intensity;
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
