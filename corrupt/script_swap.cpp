#include "script_swap.h"
#include "crash_map.h"

#include <algorithm>
#include <set>

namespace corrupt
{

std::vector<ScriptSlot> script_slots(const std::vector<ScriptEngine> &engines, uint32_t first, uint32_t last,
                                     std::optional<uint32_t> engine, bool nudge)
{
    std::vector<ScriptSlot> out;
    std::set<uint32_t> taken;
    for (auto &e : engines)
    {
        if (engine && e.site != *engine)
            continue;
        for (auto &p : e.usable_places())
        {
            const auto &c = e.commands.at(p.value);
            uint32_t end = p.offset + uint32_t(c.length) - 1;
            if (p.offset < first || end > last || taken.count(p.offset))
                continue;
            ScriptSlot s;
            s.offset = p.offset;
            s.value = p.value;
            s.length = c.length;
            for (uint8_t v : e.same_length(c.length))
                if (v != p.value)
                    s.others.push_back(v);
            if (nudge ? s.length < 2 : s.others.empty())
                continue;
            taken.insert(p.offset);
            out.push_back(std::move(s));
        }
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.offset < b.offset; });
    return out;
}

std::vector<WriteEvent> apply_script_swap(const std::vector<ScriptSlot> &slots, const std::vector<uint32_t> &picks,
                                          MemoryAccess &mem, Rng &rng, const HotMask *skip)
{
    std::vector<WriteEvent> events;
    size_t size = mem.size(Target::ROM);
    for (uint32_t pick : picks)
    {
        if (pick >= slots.size())
            continue;
        auto &s = slots[pick];
        if (s.others.empty())
            continue;
        // Drawn for every pick, so skipping one leaves the others' draws as they were.
        uint8_t now = s.others[size_t(rng.range(0, int64_t(s.others.size()) - 1))];
        if (s.offset >= size || (skip && skip->is_hot(Target::ROM, s.offset)))
            continue;
        uint8_t was = mem.read(Target::ROM, s.offset);
        if (was == now)
            continue;
        mem.write(Target::ROM, s.offset, now);
        events.push_back({ Target::ROM, s.offset, was, now });
    }
    return events;
}

std::vector<WriteEvent> apply_script_nudge(const std::vector<ScriptSlot> &slots, const std::vector<uint32_t> &picks,
                                           int min, int max, MemoryAccess &mem, Rng &rng, const HotMask *skip)
{
    std::vector<WriteEvent> events;
    size_t size = mem.size(Target::ROM);
    if (min > max)
        std::swap(min, max);
    // How many non-zero deltas there are in [min, max].
    int64_t choices = int64_t(max) - min + 1 - (min <= 0 && max >= 0 ? 1 : 0);
    for (uint32_t pick : picks)
    {
        if (pick >= slots.size() || choices <= 0)
            continue;
        auto &s = slots[pick];
        if (s.length < 2)
            continue;
        uint32_t at = s.offset + uint32_t(rng.range(1, s.length - 1));
        int64_t k = rng.range(0, choices - 1);
        int delta = int(min + k);
        if (min <= 0 && delta >= 0)
            delta++; // step over 0
        if (at >= size || (skip && skip->is_hot(Target::ROM, at)))
            continue;
        uint8_t was = mem.read(Target::ROM, at);
        uint8_t now = uint8_t(int(was) + delta);
        if (was == now)
            continue;
        mem.write(Target::ROM, at, now);
        events.push_back({ Target::ROM, at, was, now });
    }
    return events;
}

} // namespace corrupt
