#include "rule.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace corrupt
{

namespace
{

// Trigger draws (chance_per_frame) get their own stream so they never shift the op's values.
constexpr uint64_t kTriggerSalt = 0x7472696767657221ULL;
// Thinning draws (intensity below one address) get theirs too.
constexpr uint64_t kThinSalt = 0x7468696e6e696e67ULL;

std::vector<uint32_t> pick_distinct(const AddressRange &range, uint32_t count, Rng &rng)
{
    uint32_t span = range.length();
    std::vector<uint32_t> out;

    if (count >= span)
    {
        for (uint32_t a = range.first; a <= range.last; a++)
            out.push_back(a);
        return out;
    }

    if (count > span / 2)
    {
        // Dense: partial Fisher-Yates over the whole span.
        std::vector<uint32_t> all(span);
        for (uint32_t i = 0; i < span; i++)
            all[i] = range.first + i;
        for (uint32_t i = 0; i < count; i++)
            std::swap(all[i], all[size_t(rng.range(i, span - 1))]);
        all.resize(count);
        return all;
    }

    // Sparse: rejection keeps it cheap for big ranges like all of WRAM.
    std::unordered_set<uint32_t> seen;
    while (out.size() < count)
    {
        auto a = uint32_t(rng.range(range.first, range.last));
        if (seen.insert(a).second)
            out.push_back(a);
    }
    return out;
}

// Scales toward zero but never all the way: a nonzero setting stays at least +-1,
// so a low intensity means "gentle", not "does nothing".
int scale(int value, double intensity)
{
    int scaled = int(std::lround(value * intensity));
    if (scaled == 0 && value != 0)
        return value > 0 ? 1 : -1;
    return scaled;
}

} // namespace

Op scaled_op(const Op &op, double intensity)
{
    if (intensity >= 1.0)
        return op;
    Op out = op;
    if (op.type == OpType::Add)
    {
        out.min = scale(op.min, intensity);
        out.max = scale(op.max, intensity);
    }
    else if (op.type == OpType::Bitflip && !op.mask)
        out.bits = std::max(1, scale(op.bits, intensity));
    return out;
}

std::vector<WriteEvent> tick_rule(const Rule &rule, RuleRuntime &rt, MemoryAccess &mem,
                                  uint64_t seed, uint32_t rule_index, uint64_t frame,
                                  uint16_t pressed_hotkeys, double scale, const HotMask *hot,
                                  const HotMask *locked, const AimedBytes *aimed)
{
    double intensity = std::clamp(rule.intensity * scale, 0.0, 1.0);
    if (!rule.enabled || intensity <= 0.0)
        return {};

    Rng trigger_rng = Rng::for_firing(seed ^ kTriggerSalt, rule_index, frame);
    if (!should_fire(rule.when, frame, trigger_rng, pressed_hotkeys))
        return {};

    // Below one address, intensity becomes the chance to fire at all, so a gentle
    // setting still means "less" for count-1 rules. Skipped frames aren't firings,
    // which keeps the values of the firings that do happen replayable.
    double expected = rule.count * intensity;
    if (expected < 1.0)
    {
        Rng thin = Rng::for_firing(seed ^ kThinSalt, rule_index, frame);
        if (!thin.chance(expected))
            return {};
    }

    // An aimed rule with nothing to aim at (no code map, or no jump tables yet) fires on nothing.
    bool swapping = is_jump_op(rule.op.type);
    bool killing = is_function_op(rule.op.type);
    bool aiming = needs_aim(rule) && !swapping && !killing;
    if (aiming && (!aimed || !aimed->bytes || aimed->bytes->empty()))
        return {};
    if (swapping && (!aimed || !aimed->swaps || aimed->swaps->empty()))
        return {};
    if (killing && (!aimed || !aimed->functions || aimed->functions->empty()))
        return {};

    Rng rng = Rng::for_firing(seed, rule_index, rt.firings++);
    const HotMask *skip = rule.avoid_hot && hot && !hot->empty() ? hot
                          : locked && !locked->empty()              ? locked
                                                                    : nullptr;
    if (is_area_op(rule.op.type))
        return apply_area_op(rule.op, area_of(rule.range, rule.rect), mem, rng, skip);

    uint32_t count = std::max<uint32_t>(1, uint32_t(std::lround(expected)));
    // A rectangle picks among its cells; a plain range as before, so old recipes replay.
    auto pick = [&](uint32_t n) {
        if (swapping) // slot numbers
            return pick_distinct({ rule.range.target, 0, uint32_t(aimed->swaps->size() - 1) }, n, rng);
        if (killing) // function numbers
            return pick_distinct({ rule.range.target, 0, uint32_t(aimed->functions->size() - 1) }, n, rng);
        if (aiming)
        {
            auto &bytes = *aimed->bytes;
            auto picks = pick_distinct({ rule.range.target, 0, uint32_t(bytes.size() - 1) }, n, rng);
            for (auto &p : picks)
                p = bytes[p];
            return picks;
        }
        if (!rule.rect)
            return pick_distinct(rule.range, n, rng);
        Area area = area_of(rule.range, rule.rect);
        auto cells = pick_distinct({ rule.range.target, 0, area.cells() - 1 }, n, rng);
        for (auto &c : cells)
            c = area.cell(c);
        return cells;
    };

    std::vector<uint32_t> addresses;
    if (rule.pick == Pick::Sticky)
    {
        if (rt.sticky.empty())
            rt.sticky = pick(rule.count);
        addresses.assign(rt.sticky.begin(), rt.sticky.begin() + std::min<size_t>(count, rt.sticky.size()));
    }
    else
        addresses = pick(count);

    if (is_code_op(rule.op.type))
        return apply_code_op(rule.op, mem, aimed->flags, addresses, rng, skip);
    if (swapping)
        return apply_pointer_swap(*aimed->swaps, addresses, rule.op.mutual, mem, rng, skip);
    if (killing)
    {
        std::vector<WriteEvent> events;
        for (uint32_t i : addresses)
        {
            const Function &f = (*aimed->functions)[i];
            uint8_t ret = return_byte(f.type);
            if (!ret || f.entry >= mem.size(Target::ROM) || (skip && skip->is_hot(Target::ROM, f.entry)))
                continue;
            uint8_t old = mem.read(Target::ROM, f.entry);
            if (old == ret)
                continue;
            mem.write(Target::ROM, f.entry, ret);
            events.push_back({ Target::ROM, f.entry, old, ret });
        }
        return events;
    }
    return apply_op(scaled_op(rule.op, intensity), rt.op_state, mem, rule.range, addresses, rng, skip);
}

std::vector<uint32_t> aim_rule(const Rule &rule, std::span<const uint8_t> rom, std::span<const uint8_t> flags,
                               const HotMask *busy, const HotMask *low_risk)
{
    if (rule.range.target != Target::ROM || flags.empty() || is_jump_op(rule.op.type) || is_function_op(rule.op.type))
        return {};
    auto out = is_code_op(rule.op.type) ? code_op_targets(rule.op.type, rom, flags, rule.range.first, rule.range.last, rule.aim)
                                        : aimed_bytes(rom, flags, rule.range.first, rule.range.last, rule.aim);
    if (rule.aim.in_busy_functions)
        std::erase_if(out, [&](uint32_t o) { return !busy || !busy->is_hot(Target::ROM, o); });
    if (rule.aim.low_risk)
        std::erase_if(out, [&](uint32_t o) { return !low_risk || !low_risk->is_hot(Target::ROM, o); });
    return out;
}

std::optional<std::string> validate_rule(const Rule &rule, const MemoryAccess &mem)
{
    if (auto err = validate_range(rule.range, mem))
        return "range: " + *err;
    Area area = area_of(rule.range, rule.rect);
    if (rule.rect)
    {
        if (auto err = validate_area(area, mem))
            return "rect: " + *err;
        if (area.last() != rule.range.last)
            return "rect: its bottom-right byte doesn't match the range";
    }
    if (auto err = validate_op(rule.op, rule.range, mem))
        return std::string("op: ") + *err;
    if (auto err = validate_area_op(rule.op, area, mem))
        return std::string("op: ") + *err;
    if (rule.aim.active())
    {
        if (rule.range.target != Target::ROM)
            return "role and classes work on the rom target only";
        if (is_area_op(rule.op.type))
            return "role and classes don't go with area ops";
        if ((rule.aim.role == Role::Data || rule.aim.role == Role::Dma) &&
            (!rule.aim.classes.empty() || !rule.aim.except.empty()))
            return "classes only pick code, not data";
    }
    if (auto err = validate_trigger(rule.when))
        return "when: " + *err;
    if (rule.count < 1)
        return "count must be at least 1";
    if (rule.intensity < 0.0 || rule.intensity > 1.0)
        return "intensity must be within 0..1";
    return std::nullopt;
}

void set_rect(Rule &rule, Target target, uint32_t start, Rect rect)
{
    rule.rect = rect;
    rule.range = { target, start, start };
    rule.range.last = area_of(rule.range, rule.rect).last();
    rule.region.clear();
}

} // namespace corrupt
