#include "rom_blame.h"
#include "code_map.h"
#include "rng.h"

#include <algorithm>

namespace corrupt
{

RomBlame::RomBlame(RomKeeper &rom) : rom(rom)
{
    rom.on_change = [this](uint32_t offset, bool now_changed) { changed(offset, now_changed); };
    rom.on_forget = [this] {
        if (flags)
            for (auto o : offsets)
                if (o < flags_size)
                    flags[o] &= uint8_t(~codeflag::Changed);
        offsets.clear();
        uses.clear();
        index.clear();
        survivals.clear();
    };
}

RomBlame::~RomBlame()
{
    rom.on_change = nullptr;
    rom.on_forget = nullptr;
}

void RomBlame::attach(uint8_t *f, size_t size)
{
    flags = size ? f : nullptr;
    flags_size = flags ? size : 0;
    // Touches from before (or from a map that wasn't watched) say nothing about now. The
    // new map learns which bytes are changed.
    if (flags)
        for (auto o : offsets)
            if (o < flags_size)
                flags[o] = uint8_t((flags[o] & ~codeflag::Touched) | codeflag::Changed);
}

void RomBlame::set_policy(RomPolicy p, uint64_t s)
{
    pol = p;
    seed = s;
}

void RomBlame::changed(uint32_t offset, bool now_changed)
{
    auto it = index.find(offset);
    if (now_changed)
    {
        if (it != index.end() || pinned.count(offset))
            return;
        index.emplace(offset, offsets.size());
        offsets.push_back(offset);
        ByteUse u;
        u.changed_at = frame;
        uses.push_back(u);
        if (flags && offset < flags_size) // a touch was a use of the old value
            flags[offset] = uint8_t((flags[offset] & ~codeflag::Touched) | codeflag::Changed);
        return;
    }
    if (it == index.end())
        return;
    if (flags && offset < flags_size)
        flags[offset] &= uint8_t(~codeflag::Changed);
    size_t i = it->second, last = offsets.size() - 1;
    if (i != last)
    {
        offsets[i] = offsets[last];
        uses[i] = uses[last];
        index[offsets[i]] = i;
    }
    offsets.pop_back();
    uses.pop_back();
    index.erase(it);
}

void RomBlame::set_pinned(const std::vector<uint32_t> &offsets)
{
    pinned = { offsets.begin(), offsets.end() };
    for (auto o : offsets)
        if (index.count(o))
            changed(o, false); // stop watching it
}

void RomBlame::collect(uint64_t now)
{
    frame = now;
    just_used.clear();
    if (!flags)
        return;
    for (size_t i = 0; i < offsets.size(); i++)
    {
        uint32_t o = offsets[i];
        ByteUse &u = uses[i];
        if (o < flags_size && (flags[o] & codeflag::Touched))
        {
            flags[o] &= uint8_t(~codeflag::Touched);
            just_used.push_back(o);
            if (u.first_used == ByteUse::kNever)
                u.first_used = now;
            u.last_used = now;
            u.frames_used++;
        }
        if (u.frames_used && !u.survived && now >= u.first_used && now - u.first_used >= kSurviveFrames)
        {
            u.survived = true;
            survivals.push_back(o);
        }
    }
}

std::vector<uint32_t> RomBlame::take_survivals()
{
    return std::exchange(survivals, {});
}

size_t RomBlame::used() const
{
    return size_t(std::count_if(uses.begin(), uses.end(), [](const ByteUse &u) { return u.frames_used > 0; }));
}

size_t RomBlame::survivor_count(uint32_t min_frames) const
{
    return size_t(std::count_if(uses.begin(), uses.end(),
                                [&](const ByteUse &u) { return u.survived && u.frames_used >= min_frames; }));
}

const ByteUse *RomBlame::use(uint32_t offset) const
{
    auto it = index.find(offset);
    return it == index.end() ? nullptr : &uses[it->second];
}

size_t RomBlame::suspects(uint64_t after, uint64_t through, bool only_new) const
{
    if (!flags)
        return 0;
    size_t n = 0;
    for (auto &u : uses)
        n += in_window(u, after, through, only_new);
    return n;
}

ByteBlameResult RomBlame::act(uint64_t after, uint64_t through, unsigned attempt, bool only_new)
{
    ByteBlameResult out;
    if (!flags)
        return out;
    // Newly reached (first used inside the window), newest first; then the ones that were
    // already running, least proven first.
    std::vector<size_t> fresh, running;
    for (size_t i = 0; i < uses.size(); i++)
        if (in_window(uses[i], after, through, only_new))
            (uses[i].first_used > after ? fresh : running).push_back(i);
    auto by_offset = [&](size_t a, size_t b) { return offsets[a] < offsets[b]; };
    std::sort(fresh.begin(), fresh.end(), [&](size_t a, size_t b) {
        return uses[a].first_used != uses[b].first_used ? uses[a].first_used > uses[b].first_used : by_offset(a, b);
    });
    std::sort(running.begin(), running.end(), [&](size_t a, size_t b) {
        return uses[a].frames_used != uses[b].frames_used ? uses[a].frames_used < uses[b].frames_used : by_offset(a, b);
    });

    // The first crash in a row takes the likeliest half, the second all of that group,
    // later ones everything in the window.
    std::vector<size_t> chosen;
    auto &group = fresh.empty() ? running : fresh;
    if (attempt <= 1)
        chosen.assign(group.begin(), group.begin() + std::ptrdiff_t((group.size() + 1) / 2));
    else if (attempt == 2)
        chosen = group;
    else
    {
        chosen = fresh;
        chosen.insert(chosen.end(), running.begin(), running.end());
    }
    if (chosen.empty())
        return out;

    std::vector<uint32_t> picked;
    for (size_t i : chosen)
        picked.push_back(offsets[i]);
    return fix(picked);
}

ByteBlameResult RomBlame::fix(const std::vector<uint32_t> &bytes)
{
    ByteBlameResult out;
    std::vector<std::pair<uint32_t, uint8_t>> writes;
    const auto &clean = rom.clean();
    for (uint32_t o : bytes)
    {
        if (!index.count(o) || o >= clean.size())
            continue; // not changed (any more), or not ours to touch
        out.offsets.push_back(o);
        uint8_t value = clean[o];
        if (pol == RomPolicy::Reroll)
        {
            uint8_t bad = rom.read(Target::ROM, o);
            Rng rng(seed ^ (uint64_t(o) * 0x9e3779b97f4a7c15ULL) ^ (++rerolls << 32));
            do
                value = uint8_t(rng.next_u64());
            while (value == bad || value == clean[o]);
        }
        writes.emplace_back(o, value);
    }
    if (writes.empty())
        return out;
    rom.settle(writes); // reverted bytes stop being watched
    if (pol == RomPolicy::Reroll)
        for (auto [o, value] : writes)
            if (auto it = index.find(o); it != index.end())
            {
                ByteUse fresh_use;
                fresh_use.changed_at = frame;
                uses[it->second] = fresh_use; // a new value starts unproven
            }

    size_t n = out.offsets.size();
    out.text = std::string(pol == RomPolicy::Reroll ? "re-rolled " : "reverted ") + std::to_string(n) +
               (n == 1 ? " ROM byte" : " ROM bytes");
    return out;
}

} // namespace corrupt
