#include "pointer_swap.h"
#include "crash_map.h"

#include <algorithm>
#include <map>

namespace corrupt
{

std::vector<SwapSlot> swap_slots(const std::vector<JumpTable> &tables, uint32_t first, uint32_t last, bool likely)
{
    // Every dispatcher's targets (one donor per target, the first entry holding it in ROM order).
    std::map<uint32_t, std::map<uint32_t, SwapDonor>> by_site;
    for (auto &t : tables)
        for (auto &e : t.entries)
            if (e.seen || likely)
                by_site[t.site].try_emplace(e.target, SwapDonor{ e.target, e.offset, t.stride });

    std::vector<SwapSlot> out;
    for (auto &t : tables)
        for (auto &e : t.entries)
        {
            if (!e.seen || e.offset < first || e.offset + uint32_t(t.stride) - 1 > last)
                continue;
            SwapSlot slot{ e.offset, t.stride, e.target, {} };
            for (auto &[target, donor] : by_site[t.site])
            {
                if (target == e.target)
                    continue;
                if (t.stride == 2 && (target >> 16) != (e.target >> 16))
                    continue; // a 2-byte entry can't hold another bank
                if (donor.offset >= e.offset && donor.offset < e.offset + uint32_t(t.stride))
                    continue;
                slot.donors.push_back(donor);
            }
            std::sort(slot.donors.begin(), slot.donors.end(), [](auto &a, auto &b) { return a.offset < b.offset; });
            if (!slot.donors.empty())
                out.push_back(std::move(slot));
        }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.offset < b.offset; });
    // Two tables of one site can share an entry: keep it once.
    out.erase(std::unique(out.begin(), out.end(), [](auto &a, auto &b) { return a.offset == b.offset; }), out.end());
    return out;
}

namespace
{

// Writes `target` into the entry at `offset`, unless a byte of it is skipped.
void write_entry(MemoryAccess &mem, uint32_t offset, int stride, uint32_t target, const HotMask *skip,
                 std::vector<WriteEvent> &events)
{
    size_t size = mem.size(Target::ROM);
    for (int i = 0; i < stride; i++)
        if (offset + uint32_t(i) >= size || (skip && skip->is_hot(Target::ROM, offset + uint32_t(i))))
            return;
    for (int i = 0; i < stride; i++)
    {
        uint32_t o = offset + uint32_t(i);
        uint8_t was = mem.read(Target::ROM, o);
        uint8_t now = uint8_t(target >> (8 * i));
        if (was == now)
            continue;
        mem.write(Target::ROM, o, now);
        events.push_back({ Target::ROM, o, was, now });
    }
}

} // namespace

std::vector<WriteEvent> apply_pointer_swap(const std::vector<SwapSlot> &slots, const std::vector<uint32_t> &picks,
                                           bool mutual, MemoryAccess &mem, Rng &rng, const HotMask *skip)
{
    std::vector<WriteEvent> events;
    for (uint32_t pick : picks)
    {
        if (pick >= slots.size())
            continue;
        auto &slot = slots[pick];
        // Drawn for every pick, so skipping one leaves the others' draws as they were.
        auto &donor = slot.donors[size_t(rng.range(0, int64_t(slot.donors.size()) - 1))];
        write_entry(mem, slot.offset, slot.stride, donor.target, skip, events);
        if (mutual && donor.stride == slot.stride)
            write_entry(mem, donor.offset, donor.stride, slot.target, skip, events);
    }
    return events;
}

} // namespace corrupt
