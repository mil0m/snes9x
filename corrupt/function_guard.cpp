#include "function_guard.h"

#include <algorithm>
#include <map>
#include <unordered_set>

namespace corrupt
{

std::vector<InPlay> functions_in_play(const Recipe &recipe, const std::vector<uint32_t> &window_off)
{
    std::map<uint32_t, bool> deep;
    for (auto &s : recipe.switches)
        deep.try_emplace(s.entry, false);
    for (auto &c : recipe.changes)
        deep.try_emplace(c.function, false);
    for (auto &p : recipe.protects)
        deep[p.entry] = deep[p.entry] || p.deep;
    for (auto e : window_off)
        deep.try_emplace(e, false);
    std::vector<InPlay> out;
    for (auto &[entry, d] : deep)
        out.push_back({ entry, d });
    return out;
}

HotMask guard_mask(const std::vector<InPlay> &in_play, const FunctionBodies &bodies, const CallLog *calls,
                   size_t deep_limit)
{
    HotMask mask;
    auto guard = [&](const FunctionBody &b) {
        for (auto &[first, last] : b.runs())
            for (uint32_t o = first; o <= last; o++)
                mask.add(Target::ROM, o);
    };
    for (auto &f : in_play)
    {
        auto it = bodies.find(f.entry);
        if (it == bodies.end())
            continue;
        guard(it->second);
        if (!f.deep || !calls)
            continue;
        // The functions it calls, and theirs, breadth first.
        std::unordered_set<uint32_t> seen{ f.entry };
        std::vector<const FunctionBody *> todo{ &it->second };
        size_t added = 0;
        for (size_t i = 0; i < todo.size() && added < deep_limit; i++)
        {
            const FunctionBody &b = *todo[i];
            std::vector<uint32_t> called;
            for (auto &[key, seen_call] : calls->all())
                if (b.contains(key.site))
                    called.push_back(key.target);
            std::sort(called.begin(), called.end());
            for (auto target : called)
            {
                if (added >= deep_limit || !seen.insert(target).second)
                    continue;
                auto c = bodies.find(target);
                if (c == bodies.end())
                    continue;
                guard(c->second);
                todo.push_back(&c->second);
                added++;
            }
        }
    }
    return mask;
}

HotMask own_bytes(const Recipe &recipe, const std::vector<uint32_t> &window_off,
                  const std::vector<Change> &window_changes)
{
    HotMask own;
    for (auto &p : recipe.pins)
        own.add(Target::ROM, p.offset);
    for (auto &s : recipe.switches)
        own.add(Target::ROM, s.entry);
    for (auto e : window_off)
        own.add(Target::ROM, e);
    for (auto *list : { &recipe.changes, &window_changes })
        for (auto &c : *list)
            for (size_t i = 0; i < c.value.size(); i++)
                own.add(Target::ROM, uint32_t(c.at + i));
    return own;
}

std::vector<std::pair<uint32_t, uint8_t>> guard_cleanup(const HotMask &guarded, const HotMask &own,
                                                        std::span<const uint8_t> clean, std::span<const uint8_t> now)
{
    std::vector<std::pair<uint32_t, uint8_t>> out;
    auto &g = guarded.hot[size_t(Target::ROM)];
    size_t n = std::min({ g.size(), clean.size(), now.size() });
    for (size_t o = 0; o < n; o++)
        if (g[o] && now[o] != clean[o] && !own.is_hot(Target::ROM, uint32_t(o)))
            out.push_back({ uint32_t(o), clean[o] });
    return out;
}

std::vector<uint8_t> function_marks(const std::vector<ShownFunction> &functions, const HotMask &guarded,
                                    size_t rom_size)
{
    std::vector<uint8_t> marks(rom_size, 0);
    for (auto &f : functions)
        for (auto &[first, last] : f.runs)
            for (uint32_t o = first; o <= last && o < rom_size; o++)
                marks[o] = std::max(marks[o], guarded.is_hot(Target::ROM, o) ? funcmark::Guarded : funcmark::Body);
    for (auto &f : functions)
        if (f.off && f.entry < rom_size)
            marks[f.entry] = funcmark::Off;
    return marks;
}

const ShownFunction *function_at(const std::vector<ShownFunction> &functions, uint32_t offset)
{
    const ShownFunction *best = nullptr;
    for (auto &f : functions)
    {
        bool in = false;
        for (auto &[first, last] : f.runs)
            if (offset >= first && offset <= last)
            {
                in = true;
                break;
            }
        if (!in)
            continue;
        if (!best || (f.guarded && !best->guarded) || (f.guarded == best->guarded && f.bytes < best->bytes))
            best = &f;
    }
    return best;
}

} // namespace corrupt
