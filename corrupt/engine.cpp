#include "engine.h"

#include <algorithm>

namespace corrupt
{

void Engine::set_recipe(Recipe recipe)
{
    current = std::move(recipe);
    ids.clear();
    for (size_t i = 0; i < current.rules.size(); i++)
        ids.push_back(next_id++);
    restart();
}

void Engine::replace_rule(size_t index, Rule rule)
{
    if (index >= current.rules.size())
        return;
    current.rules[index] = std::move(rule);
    ids[index] = next_id++;
    runtimes[index] = RuleRuntime{};
    mutes[index] = 0;
}

size_t Engine::add_rule(Rule rule)
{
    current.rules.push_back(std::move(rule));
    ids.push_back(next_id++);
    runtimes.emplace_back();
    mutes.push_back(0);
    return current.rules.size() - 1;
}

void Engine::remove_rule(size_t index)
{
    if (index >= current.rules.size())
        return;
    current.rules.erase(current.rules.begin() + std::ptrdiff_t(index));
    ids.erase(ids.begin() + std::ptrdiff_t(index));
    runtimes.erase(runtimes.begin() + std::ptrdiff_t(index));
    mutes.erase(mutes.begin() + std::ptrdiff_t(index));

    std::deque<LoggedEvent> kept;
    for (auto &ev : log)
    {
        if (ev.rule_index == index)
            continue;
        kept.push_back(ev);
        if (ev.rule_index > index)
            kept.back().rule_index--;
    }
    log = std::move(kept);
}

uint64_t Engine::rule_id(size_t index) const
{
    return index < ids.size() ? ids[index] : 0;
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

void Engine::set_info(std::string name, std::string notes)
{
    current.name = std::move(name);
    current.notes = std::move(notes);
}

void Engine::set_intensity(double intensity)
{
    current.intensity = std::clamp(intensity, 0.0, 1.0);
}

void Engine::set_anchor_state(std::string file)
{
    current.anchor_state = std::move(file);
}

void Engine::rename_region(const std::string &from, const std::string &to)
{
    for (auto &rule : current.rules)
        if (rule.region == from)
            rule.region = to;
}

void Engine::restart()
{
    runtimes.assign(current.rules.size(), RuleRuntime{});
    mutes.assign(current.rules.size(), 0); // they refer to frames of the previous run
    log.clear();
    frame = 0;
    pending_hotkeys = 0;
    released = false;
}

size_t Engine::fire_at_load(MemoryAccess &mem, const std::function<bool(Target)> &which)
{
    if (!is_active)
        return 0;
    size_t written = 0;
    for (uint32_t i = 0; i < current.rules.size(); i++)
    {
        auto &rule = current.rules[i];
        if (rule.when.type != TriggerType::AtLoad || runtimes[i].firings > 0)
            continue;
        if (which && !which(rule.range.target))
            continue;
        auto events = tick_rule(rule, runtimes[i], mem, Rng::rule_seed(current.seed, rule.salt), i, 0, 0,
                                current.intensity, &hot_and_locked, &locked);
        for (auto &e : events)
            log.push_back({ 0, i, e });
        written += events.size();
    }
    return written;
}

void Engine::set_hot_mask(HotMask mask)
{
    hot = std::move(mask);
    hot_and_locked = hot;
    hot_and_locked.merge(locked);
}

void Engine::set_locked(HotMask mask)
{
    locked = std::move(mask);
    hot_and_locked = hot;
    hot_and_locked.merge(locked);
}

size_t Engine::apply_patches(MemoryAccess &mem, const std::function<bool(Target)> &which)
{
    size_t written = 0;
    for (auto &p : current.patches)
    {
        if (which && !which(p.target))
            continue;
        for (uint32_t i = 0; i < p.bytes.size(); i++)
        {
            uint32_t at = p.at + i;
            if (at >= mem.size(p.target) || locked.is_hot(p.target, at) || mem.read(p.target, at) == p.bytes[i])
                continue;
            mem.write_at_start(p.target, at, p.bytes[i]);
            written++;
        }
    }
    return written;
}

bool Engine::has_patches(Target t) const
{
    for (auto &p : current.patches)
        if (p.target == t)
            return true;
    return false;
}

void Engine::add_patches(Target t, const std::vector<std::pair<uint32_t, uint8_t>> &bytes)
{
    add_patch_bytes(current.patches, t, bytes);
}

void Engine::clear_patches(Target t)
{
    std::erase_if(current.patches, [t](const Patch &p) { return p.target == t; });
}

bool Engine::has_at_load(Target t) const
{
    for (auto &rule : current.rules)
        if (rule.enabled && rule.when.type == TriggerType::AtLoad && rule.range.target == t)
            return true;
    return false;
}

void Engine::on_frame(MemoryAccess &mem)
{
    if (!is_active || waiting())
        return;

    frame++;
    uint16_t hotkeys = pending_hotkeys;
    pending_hotkeys = 0;

    for (uint32_t i = 0; i < current.rules.size(); i++)
    {
        if (frame <= mutes[i])
            continue;
        auto &rule = current.rules[i];
        auto events = tick_rule(rule, runtimes[i], mem, Rng::rule_seed(current.seed, rule.salt), i, frame, hotkeys,
                                current.intensity, &hot_and_locked, &locked);
        for (auto &e : events)
            log.push_back({ frame, i, e });
    }

    while (log.size() > kEventLogLimit)
        log.pop_front();
}

EngineSnapshot Engine::snapshot() const
{
    return { frame, ids, runtimes };
}

void Engine::restore(const EngineSnapshot &snap)
{
    frame = snap.frame;
    for (size_t i = 0; i < ids.size(); i++)
    {
        runtimes[i] = RuleRuntime{};
        for (size_t j = 0; j < snap.rule_ids.size() && j < snap.runtimes.size(); j++)
            if (snap.rule_ids[j] == ids[i])
                runtimes[i] = snap.runtimes[j];
    }
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
