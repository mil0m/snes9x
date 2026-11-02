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
    aimed.erase(ids[index]);
    swaps.erase(ids[index]);
    kills.erase(ids[index]);
    run_flags = {}; // an edited rule aims with the map as it is now
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
    aimed.erase(ids[index]);
    swaps.erase(ids[index]);
    kills.erase(ids[index]);
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
    flips.clear();
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
    aimed.clear(); // the next run aims at the map as it is then
    run_flags = {};
    swaps.clear();
    run_jumps.clear();
    kills.clear();
    run_functions.clear();
    run_busy = {};
    run_busy_taken = false;
    flips.clear();
    frame = 0;
    pending_hotkeys = 0;
    vetoed.clear(); // frames of the previous run
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
                                current.intensity, &hot_and_locked, &rules_locked, aim_for(i, mem));
        for (auto &e : events)
            log.push_back({ 0, i, e });
        written += events.size();
    }
    return written;
}

void Engine::merge_masks()
{
    rules_locked = locked;
    rules_locked.merge(guarded);
    hot_and_locked = hot;
    hot_and_locked.merge(rules_locked);
}

void Engine::set_hot_mask(HotMask mask)
{
    hot = std::move(mask);
    merge_masks();
}

void Engine::set_locked(HotMask mask)
{
    locked = std::move(mask);
    merge_masks();
}

void Engine::set_guarded(HotMask mask)
{
    guarded = std::move(mask);
    merge_masks();
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
            if (at >= mem.size(p.target) || rules_locked.is_hot(p.target, at) || mem.read(p.target, at) == p.bytes[i])
                continue;
            mem.write_at_start(p.target, at, p.bytes[i]);
            written++;
        }
    }
    return written;
}

Engine::PinsApplied Engine::apply_pins(MemoryAccess &mem)
{
    PinsApplied out;
    size_t size = mem.size(Target::ROM);
    for (auto &pin : current.pins)
    {
        if (pin.offset >= size || locked.is_hot(Target::ROM, pin.offset))
            continue;
        uint8_t now = mem.read(Target::ROM, pin.offset);
        if (now != pin.was)
        {
            out.mismatched++;
            continue;
        }
        if (now == pin.value)
            continue;
        mem.write_at_start(Target::ROM, pin.offset, pin.value);
        out.written++;
    }
    return out;
}

void Engine::add_pins(const std::vector<Pin> &pins)
{
    for (auto &pin : pins)
    {
        auto it = std::find_if(current.pins.begin(), current.pins.end(),
                               [&](const Pin &p) { return p.offset == pin.offset; });
        if (it != current.pins.end())
            *it = pin;
        else
            current.pins.push_back(pin);
    }
    std::sort(current.pins.begin(), current.pins.end(), [](const Pin &a, const Pin &b) { return a.offset < b.offset; });
}

void Engine::flip_function(uint32_t entry, uint8_t ret, uint8_t clean)
{
    flip_bytes({ entry, { ret }, { clean } });
}

void Engine::flip_bytes(Flip f)
{
    if (is_active && !f.on.empty() && f.on.size() == f.off.size())
        flips.push_back(std::move(f));
}

void Engine::add_change(const Change &c)
{
    auto it = std::find_if(current.changes.begin(), current.changes.end(), [&](const Change &o) { return o.at == c.at; });
    if (it != current.changes.end())
        *it = c;
    else
        current.changes.push_back(c);
    std::sort(current.changes.begin(), current.changes.end(), [](const Change &a, const Change &b) { return a.at < b.at; });
}

void Engine::remove_change(uint32_t at)
{
    std::erase_if(current.changes, [&](const Change &c) { return c.at == at; });
}

Engine::PinsApplied Engine::apply_changes(MemoryAccess &mem)
{
    PinsApplied out;
    size_t size = mem.size(Target::ROM);
    for (auto &c : current.changes)
    {
        if (!c.start_on || c.value.empty() || c.value.size() != c.was.size() || c.at + c.value.size() > size)
            continue;
        bool locked_byte = false, is_value = true, is_was = true;
        for (size_t i = 0; i < c.value.size(); i++)
        {
            uint32_t o = uint32_t(c.at + i);
            locked_byte |= locked.is_hot(Target::ROM, o);
            uint8_t now = mem.read(Target::ROM, o);
            is_value &= now == c.value[i];
            is_was &= now == c.was[i];
        }
        if (locked_byte || is_value)
            continue;
        if (!is_was)
        {
            out.mismatched++;
            continue;
        }
        for (size_t i = 0; i < c.value.size(); i++)
            mem.write_at_start(Target::ROM, uint32_t(c.at + i), c.value[i]);
        out.written++;
    }
    return out;
}

void Engine::add_switch(const Switch &s)
{
    auto it = std::find_if(current.switches.begin(), current.switches.end(),
                           [&](const Switch &o) { return o.entry == s.entry; });
    if (it != current.switches.end())
        *it = s;
    else
        current.switches.push_back(s);
    std::sort(current.switches.begin(), current.switches.end(),
              [](const Switch &a, const Switch &b) { return a.entry < b.entry; });
}

void Engine::remove_switch(uint32_t entry)
{
    std::erase_if(current.switches, [&](const Switch &s) { return s.entry == entry; });
}

void Engine::add_protect(const Protect &p)
{
    auto it = std::find_if(current.protects.begin(), current.protects.end(),
                           [&](const Protect &o) { return o.entry == p.entry; });
    if (it != current.protects.end())
        *it = p;
    else
        current.protects.push_back(p);
    std::sort(current.protects.begin(), current.protects.end(),
              [](const Protect &a, const Protect &b) { return a.entry < b.entry; });
}

void Engine::remove_protect(uint32_t entry)
{
    std::erase_if(current.protects, [&](const Protect &p) { return p.entry == entry; });
}

Engine::PinsApplied Engine::apply_switches(MemoryAccess &mem)
{
    PinsApplied out;
    size_t size = mem.size(Target::ROM);
    for (auto &s : current.switches)
    {
        if (!s.start_off || s.entry >= size || locked.is_hot(Target::ROM, s.entry))
            continue;
        uint8_t now = mem.read(Target::ROM, s.entry);
        if (now == s.ret)
            continue;
        if (now != s.was)
        {
            out.mismatched++;
            continue;
        }
        mem.write_at_start(Target::ROM, s.entry, s.ret);
        out.written++;
    }
    return out;
}

void Engine::remove_pins(const std::vector<uint32_t> &offsets)
{
    std::erase_if(current.pins, [&](const Pin &p) {
        return std::find(offsets.begin(), offsets.end(), p.offset) != offsets.end();
    });
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

void Engine::remove_patches(Target t, const std::vector<uint32_t> &offsets)
{
    remove_patch_bytes(current.patches, t, offsets);
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

namespace
{

// Passes everything through but the writes in `skip` (a frame's vetoes, or all of them for a
// preview), which never land.
struct FilteredMemory : MemoryAccess
{
    MemoryAccess &inner;
    std::function<bool(Target, uint32_t)> skip;
    FilteredMemory(MemoryAccess &inner, std::function<bool(Target, uint32_t)> skip) : inner(inner), skip(std::move(skip)) {}
    size_t size(Target t) const override { return inner.size(t); }
    uint8_t read(Target t, uint32_t offset) const override { return inner.read(t, offset); }
    void write(Target t, uint32_t offset, uint8_t value) override
    {
        if (!skip(t, offset))
            inner.write(t, offset, value);
    }
    void write_at_start(Target t, uint32_t offset, uint8_t value) override
    {
        if (!skip(t, offset))
            inner.write_at_start(t, offset, value);
    }
    const uint8_t *data(Target t) const override { return inner.data(t); }
};

// Keeps every write to itself, on top of the memory underneath, which never changes: what a
// preview writes reads back (the ops log what memory holds after a write) and lands nowhere.
struct OverlayMemory : MemoryAccess
{
    MemoryAccess &inner;
    std::unordered_map<uint64_t, uint8_t> written;
    explicit OverlayMemory(MemoryAccess &inner) : inner(inner) {}
    static uint64_t key(Target t, uint32_t offset) { return (uint64_t(t) << 32) | offset; }
    size_t size(Target t) const override { return inner.size(t); }
    uint8_t read(Target t, uint32_t offset) const override
    {
        auto it = written.find(key(t, offset));
        return it == written.end() ? inner.read(t, offset) : it->second;
    }
    void write(Target t, uint32_t offset, uint8_t value) override { written[key(t, offset)] = value; }
    // The whole block can't show the overlay; ops that need it read byte by byte.
    const uint8_t *data(Target t) const override { return written.empty() ? inner.data(t) : nullptr; }
};

} // namespace

void Engine::on_frame(MemoryAccess &real)
{
    if (!is_active || waiting())
        return;

    frame++;
    uint16_t hotkeys = pending_hotkeys;
    pending_hotkeys = 0;
    std::vector<Flip> flipping;
    flipping.swap(flips);
    for (auto &s : current.switches)
        if (s.hotkey >= 1 && s.hotkey <= 9 && (hotkeys & hotkey_bit(s.hotkey)))
            flipping.push_back({ s.entry, { s.ret }, { s.was } });
    for (auto &c : current.changes)
        if (c.hotkey >= 1 && c.hotkey <= 9 && (hotkeys & hotkey_bit(c.hotkey)))
            flipping.push_back({ c.at, c.value, c.was });
    // Timers (#61), from the run's start: at the start of each cycle, then after `for_frames`.
    auto timed = [&](uint32_t at, std::vector<uint8_t> on, std::vector<uint8_t> off, uint32_t every, uint32_t for_frames) {
        if (!every || !for_frames || for_frames >= every)
            return;
        uint64_t phase = (frame - 1) % every;
        if (phase == 0)
            flipping.push_back({ at, std::move(on), std::move(off), Flip::How::On, true });
        else if (phase == for_frames)
            flipping.push_back({ at, std::move(on), std::move(off), Flip::How::Off, true });
    };
    for (auto &s : current.switches)
        timed(s.entry, { s.ret }, { s.was }, s.every, s.for_frames);
    for (auto &c : current.changes)
        timed(c.at, c.value, c.was, c.every, c.for_frames);

    bool quiet = quiet_from && frame > *quiet_from;
    bool vetoes_now = quiet || std::any_of(vetoed.begin(), vetoed.end(), [&](const Veto &v) { return v.frame == frame; });
    auto is_vetoed = [&](Target t, uint32_t offset) {
        return (quiet && t == quiet_target) ||
               std::any_of(vetoed.begin(), vetoed.end(),
                           [&](const Veto &v) { return v.frame == frame && v.target == t && v.offset == offset; });
    };
    FilteredMemory filtered(real, is_vetoed);
    MemoryAccess &mem = vetoes_now ? static_cast<MemoryAccess &>(filtered) : real;

    // Kill switches and changes first: the frame runs with them as asked.
    for (auto &f : flipping)
    {
        size_t n = f.on.size();
        if (!n || n != f.off.size() || f.on == f.off || f.at + n > mem.size(Target::ROM))
            continue;
        bool locked_byte = false, is_on = true, is_off = true;
        for (size_t i = 0; i < n; i++)
        {
            locked_byte |= locked.is_hot(Target::ROM, uint32_t(f.at + i));
            uint8_t now = mem.read(Target::ROM, uint32_t(f.at + i));
            is_on &= now == f.on[i];
            is_off &= now == f.off[i];
        }
        if (locked_byte || (f.ours_only && !is_on && !is_off))
            continue;
        const std::vector<uint8_t> *to = nullptr;
        switch (f.how)
        {
        case Flip::How::Toggle: to = is_on ? &f.off : is_off ? &f.on : nullptr; break;
        case Flip::How::On: to = &f.on; break;
        case Flip::How::Off: to = &f.off; break;
        }
        if (to)
            for (size_t i = 0; i < n; i++)
                mem.write(Target::ROM, uint32_t(f.at + i), (*to)[i]);
    }

    for (uint32_t i = 0; i < current.rules.size(); i++)
    {
        auto &rule = current.rules[i];
        // A kill's functions put back on when their time is up (#61): not a rule's write.
        auto &backs = runtimes[i].put_backs;
        for (auto &b : backs)
            if (b.frame <= frame && b.write.offset < mem.size(b.write.target) &&
                mem.read(b.write.target, b.write.offset) == b.write.new_value)
                mem.write(b.write.target, b.write.offset, b.write.old_value);
        std::erase_if(backs, [&](const RuleRuntime::PutBack &b) { return b.frame <= frame; });
        if (frame <= mutes[i])
            continue;
        auto events = tick_rule(rule, runtimes[i], mem, Rng::rule_seed(current.seed, rule.salt), i, frame, hotkeys,
                                current.intensity, &hot_and_locked, &rules_locked, aim_for(i, mem));
        for (auto &e : events)
            if (!vetoes_now || !is_vetoed(e.target, e.offset))
            {
                log.push_back({ frame, i, e });
                if (is_function_op(rule.op.type) && rule.op.put_back)
                    backs.push_back({ frame + rule.op.put_back, e });
            }
    }

    while (log.size() > kEventLogLimit)
        log.pop_front();
}

const AimedBytes *Engine::aim_for(uint32_t index, MemoryAccess &mem)
{
    const Rule &rule = current.rules[index];
    if (!needs_aim(rule))
        return nullptr;
    if (is_function_op(rule.op.type))
    {
        auto found = kills.find(ids[index]);
        if (found == kills.end())
        {
            if (run_functions.empty() && learned_functions)
                run_functions = learned_functions();
            if (run_functions.empty())
            {
                aimed_now = {}; // nothing learned yet: try again next time
                return &aimed_now;
            }
            std::vector<Function> in_range;
            for (auto &f : run_functions)
                if (f.entry >= rule.range.first && f.entry <= rule.range.last)
                    in_range.push_back(f);
            found = kills.emplace(ids[index], busiest(std::move(in_range), rule.op.any ? 0 : kBusiestFunctions)).first;
        }
        else if (!rule.op.any && found->second.size() < kBusiestFunctions && learned_functions && frame % 300 == 0)
        {
            // Still learning (a game's first play): take the list again every 5 s until it's full (#61).
            run_functions = learned_functions();
            std::vector<Function> in_range;
            for (auto &f : run_functions)
                if (f.entry >= rule.range.first && f.entry <= rule.range.last)
                    in_range.push_back(f);
            found->second = busiest(std::move(in_range), kBusiestFunctions);
        }
        aimed_now = { nullptr, {}, nullptr, &found->second };
        return &aimed_now;
    }
    if (is_jump_op(rule.op.type))
    {
        auto found = swaps.find(ids[index]);
        if (found == swaps.end())
        {
            if (run_jumps.empty() && jump_tables)
                run_jumps = jump_tables();
            if (run_jumps.empty())
            {
                aimed_now = {}; // nothing known yet: try again next time
                return &aimed_now;
            }
            found = swaps.emplace(ids[index], swap_slots(run_jumps, rule.range.first, rule.range.last, rule.op.likely)).first;
        }
        aimed_now = { nullptr, {}, &found->second };
        return &aimed_now;
    }
    if (run_flags.empty() && code_flags)
        run_flags = code_flags();
    std::span<const uint8_t> flags = run_flags;
    if (flags.empty() && !aimed.count(ids[index]))
    {
        aimed_now = { nullptr, flags }; // nothing known yet: try again next time
        return &aimed_now;
    }
    if (rule.aim.in_busy_functions && !run_busy_taken && busy_code)
    {
        run_busy = busy_code();
        run_busy_taken = !run_busy.empty(); // nothing learned yet: ask again next time
    }
    if (rule.aim.in_busy_functions && run_busy.empty() && !aimed.count(ids[index]))
    {
        aimed_now = { nullptr, flags };
        return &aimed_now;
    }
    auto [it, fresh] = aimed.try_emplace(ids[index]);
    if (fresh)
    {
        size_t size = mem.size(Target::ROM);
        if (const uint8_t *raw = mem.data(Target::ROM))
            it->second = aim_rule(rule, { raw, size }, flags, &run_busy);
        else
        {
            std::vector<uint8_t> rom(size);
            for (size_t i = 0; i < size; i++)
                rom[i] = mem.read(Target::ROM, uint32_t(i));
            it->second = aim_rule(rule, rom, flags, &run_busy);
        }
    }
    aimed_now = { &it->second, flags };
    return &aimed_now;
}

size_t Engine::aimed_count(size_t index) const
{
    if (index >= ids.size())
        return 0;
    if (auto s = swaps.find(ids[index]); s != swaps.end())
        return s->second.size();
    if (auto k = kills.find(ids[index]); k != kills.end())
        return k->second.size();
    auto it = aimed.find(ids[index]);
    return it == aimed.end() ? 0 : it->second.size();
}

std::vector<LoggedEvent> Engine::preview_next(MemoryAccess &mem, std::optional<Target> only)
{
    std::vector<LoggedEvent> out;
    if (!is_active || waiting())
        return out;
    auto snap = snapshot();
    uint16_t hotkeys = pending_hotkeys;
    auto queued = flips;
    OverlayMemory nothing_lands(mem);
    on_frame(nothing_lands);
    for (auto &e : log)
        if (e.frame == frame && (!only || e.write.target == *only))
            out.push_back(e);
    restore(snap);
    pending_hotkeys = hotkeys;
    flips = std::move(queued);
    return out;
}

void Engine::veto(uint64_t at, Target target, const std::vector<uint32_t> &offsets)
{
    for (uint32_t o : offsets)
        vetoed.push_back({ at, target, o });
}

void Engine::drop_vetoes_through(uint64_t at)
{
    std::erase_if(vetoed, [&](const Veto &v) { return v.frame <= at; });
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
