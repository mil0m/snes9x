#include "explore.h"

#include "code_map.h"
#include "rng.h"
#include "rule_text.h"

#include <algorithm>
#include <cmath>

namespace corrupt
{

const char *family_name(Family f)
{
    switch (f)
    {
    case Family::Reroll:
        return "reroll";
    case Family::Nudge:
        return "nudge";
    case Family::Ram:
        return "ram";
    case Family::Code:
        return "code";
    case Family::Swap:
        return "swap";
    case Family::Kill:
        return "kill";
    case Family::Data:
        return "data";
    }
    return "?";
}

namespace
{

template <typename T> T pick(Rng &rng, std::initializer_list<T> from)
{
    return *(from.begin() + rng.range(0, int64_t(from.size()) - 1));
}

// A window of 2^k bytes (k in [lo, hi]) inside [first, last].
AddressRange window_in(Rng &rng, Target t, uint32_t first, uint32_t last, int lo, int hi)
{
    uint32_t span = last - first + 1;
    uint32_t len = std::min<uint32_t>(span, 1u << rng.range(lo, hi));
    uint32_t at = first + uint32_t(rng.range(0, int64_t(span - len)));
    return { t, at, at + len - 1 };
}

Rule ram_rule(Rng &rng, const ExploreKnows &knows, const MemoryAccess &mem)
{
    Rule r;
    int where = int(rng.range(0, 9));
    Target t = where < 6 ? Target::WRAM : where < 8 ? Target::OAM : where < 9 ? Target::CGRAM : Target::VRAM;
    uint32_t size = uint32_t(mem.size(t));
    if (t == Target::WRAM)
    {
        if (!knows.wram.empty())
        {
            auto &w = knows.wram[size_t(rng.range(0, int64_t(knows.wram.size()) - 1))];
            r.range = window_in(rng, t, w.first, w.last, 4, 10);
        }
        else // clear of the direct page and the stack, as the default rule
            r.range = window_in(rng, t, std::min<uint32_t>(0x2000, size - 1), size - 1, 4, 12);
    }
    else if (t == Target::VRAM)
        r.range = window_in(rng, t, 0, size - 1, 8, 12);
    else
        r.range = { t, 0, size - 1 };
    bool wram = t == Target::WRAM;
    auto type = wram ? pick(rng, { OpType::Add, OpType::Random, OpType::Bitflip, OpType::Freeze, OpType::Shuffle,
                                   OpType::Copy, OpType::Swap })
                     : pick(rng, { OpType::Add, OpType::Random, OpType::Bitflip, OpType::Shuffle, OpType::Copy });
    r.op.type = type;
    switch (type)
    {
    case OpType::Add:
    {
        int k = int(rng.range(1, 16));
        r.op.min = -k;
        r.op.max = k;
        break;
    }
    case OpType::Random:
        r.op.min = 0;
        r.op.max = 255;
        break;
    case OpType::Bitflip:
        r.op.bits = int(rng.range(1, 3));
        break;
    case OpType::Shuffle:
        r.op.window = int(rng.range(4, 16));
        break;
    case OpType::Copy:
        r.op.length = uint32_t(rng.range(1, 16));
        r.op.from = AddressRange{ t, 0, size - 1 };
        break;
    default:
        break;
    }
    // Strong enough to show within the window: measured, one byte now and then rarely did.
    r.count = uint32_t(rng.range(2, t == Target::VRAM ? 64 : 16));
    r.when = { .type = TriggerType::EveryFrames, .every = pick(rng, { 1u, 2u, 4u, 8u, 15u, 30u }) };
    r.pick = rng.chance(0.25) ? Pick::Sticky : Pick::Fresh;
    return r;
}

Rule rom_rule(const MemoryAccess &mem, OpType type)
{
    Rule r;
    r.range = { Target::ROM, 0, uint32_t(mem.size(Target::ROM)) - 1 };
    r.op.type = type;
    r.count = 1;
    return r;
}

Rule nudged(Rng &rng, Rule r, const MemoryAccess &mem)
{
    switch (rng.range(0, 3))
    {
    case 0: // strength
        r.intensity = std::clamp(rng.chance(0.5) ? r.intensity * 2 : r.intensity / 2, 0.05, 1.0);
        if (r.intensity == 1.0)
            r.count = std::max<uint32_t>(1, r.count * 2);
        break;
    case 1: // how many
        r.count = rng.chance(0.5) ? r.count * 2 : std::max<uint32_t>(1, r.count / 2);
        break;
    case 2: // the range moved by half its length
    {
        if (r.rect)
            break;
        uint32_t size = uint32_t(mem.size(r.range.target)), len = r.range.length();
        int64_t move = std::max<int64_t>(1, len / 2) * (rng.chance(0.5) ? 1 : -1);
        int64_t first = std::clamp<int64_t>(int64_t(r.range.first) + move, 0, int64_t(size) - len);
        r.range.first = uint32_t(first);
        r.range.last = uint32_t(first) + len - 1;
        r.region.clear();
        break;
    }
    default: // another value op
        if (r.op.type == OpType::Add || r.op.type == OpType::Random || r.op.type == OpType::Bitflip)
        {
            auto t = pick(rng, { OpType::Add, OpType::Random, OpType::Bitflip });
            r.op.type = t;
            if (t == OpType::Add && r.op.min == 0 && r.op.max == 0)
            {
                r.op.min = -4;
                r.op.max = 4;
            }
            if (t == OpType::Random)
            {
                r.op.min = 0;
                r.op.max = 255;
            }
        }
        else
            r.intensity = std::clamp(r.intensity / 2, 0.05, 1.0);
        break;
    }
    return r;
}

} // namespace

std::vector<AddressRange> busy_wram(std::span<const uint32_t> reads, size_t blocks, uint32_t block)
{
    std::vector<std::pair<uint64_t, uint32_t>> sums; // reads, first byte
    for (uint32_t at = 0; at < reads.size(); at += block)
    {
        uint32_t last = std::min<uint32_t>(at + block, uint32_t(reads.size())) - 1;
        bool stack = at <= 0x1FF || (at >= 0x1E00 && at <= 0x1FFF);
        if (stack)
            continue;
        uint64_t n = 0;
        for (uint32_t i = at; i <= last; i++)
            n += reads[i];
        if (n)
            sums.push_back({ n, at });
    }
    std::stable_sort(sums.begin(), sums.end(), [](auto &a, auto &b) { return a.first > b.first; });
    if (sums.size() > blocks)
        sums.resize(blocks);
    std::sort(sums.begin(), sums.end(), [](auto &a, auto &b) { return a.second < b.second; });
    std::vector<AddressRange> out;
    for (auto &[n, at] : sums)
    {
        uint32_t last = std::min<uint32_t>(at + block, uint32_t(reads.size())) - 1;
        if (!out.empty() && out.back().last + 1 == at)
            out.back().last = last;
        else
            out.push_back({ Target::WRAM, at, last });
    }
    return out;
}

std::vector<Variant> make_variants(const ExploreKnows &knows, const MemoryAccess &mem, size_t count, uint64_t seed,
                                   uint32_t window)
{
    std::vector<Family> families;
    bool has_rules = knows.recipe && std::any_of(knows.recipe->rules.begin(), knows.recipe->rules.end(),
                                                 [](const Rule &r) { return r.enabled; });
    if (knows.recipe)
        families.push_back(Family::Reroll);
    if (has_rules)
        families.push_back(Family::Nudge);
    families.push_back(Family::Ram);
    bool rom = mem.size(Target::ROM) > 0;
    if (rom && knows.code_map)
        families.push_back(Family::Code);
    if (rom && knows.jump_tables)
        families.push_back(Family::Swap);
    if (rom && knows.functions)
        families.push_back(Family::Kill);
    if (rom && knows.code_map)
        families.push_back(Family::Data);

    uint32_t early = std::max<uint32_t>(2, window / 3);
    std::vector<Variant> out;
    for (size_t i = 0; i < count; i++)
    {
        Rng rng(seed ^ (uint64_t(i + 1) * 0x9E3779B97F4A7C15ULL));
        Variant v;
        v.id = uint32_t(i);
        v.family = families[i % families.size()];
        Recipe r;
        r.seed = rng.next_u64() & 0xFFFFFFFF;
        Rule rule;
        switch (v.family)
        {
        case Family::Reroll:
            r = *knows.recipe;
            r.seed = rng.next_u64() & 0xFFFFFFFF;
            v.what = "its own recipe, seed " + std::to_string(r.seed);
            break;
        case Family::Nudge:
        {
            r = *knows.recipe;
            std::vector<size_t> enabled;
            for (size_t k = 0; k < r.rules.size(); k++)
                if (r.rules[k].enabled)
                    enabled.push_back(k);
            size_t k = enabled[size_t(rng.range(0, int64_t(enabled.size()) - 1))];
            r.rules[k] = nudged(rng, r.rules[k], mem);
            v.what = "its own recipe, " + r.rules[k].name + " now " + describe_rule(r.rules[k]);
            break;
        }
        case Family::Ram:
            rule = ram_rule(rng, knows, mem);
            break;
        case Family::Code:
            rule = rom_rule(mem, pick(rng, { OpType::OpcodeSwap, OpType::BranchFlip, OpType::NopOut }));
            rule.aim.in_busy_functions = knows.busy_code;
            rule.when = { .type = TriggerType::EveryFrames, .every = pick(rng, { 60u, 120u, 240u }) };
            break;
        case Family::Swap:
            rule = rom_rule(mem, OpType::PointerSwap);
            rule.when = { .type = TriggerType::OnceAtFrame, .frame = uint64_t(rng.range(1, early)) };
            break;
        case Family::Kill:
            rule = rom_rule(mem, OpType::Kill);
            rule.when = { .type = TriggerType::OnceAtFrame, .frame = uint64_t(rng.range(1, early)) };
            break;
        case Family::Data:
            rule = rom_rule(mem, pick(rng, { OpType::Add, OpType::Random, OpType::Bitflip }));
            rule.aim.role = Role::Data;
            if (rule.op.type == OpType::Add)
            {
                rule.op.min = -2;
                rule.op.max = 2;
            }
            else if (rule.op.type == OpType::Random)
                rule.op.max = 255;
            rule.count = uint32_t(rng.range(1, 4));
            rule.when = { .type = TriggerType::EveryFrames, .every = pick(rng, { 30u, 60u, 120u }) };
            break;
        }
        if (v.family != Family::Reroll && v.family != Family::Nudge)
        {
            rule.name = family_name(v.family);
            rule.enabled = true;
            r.rules = { rule };
            v.what = describe_rule(rule);
        }
        r.name = std::string("Explored ") + family_name(v.family) + " " + std::to_string(v.id + 1);
        v.recipe = std::move(r);
        out.push_back(std::move(v));
    }
    return out;
}

ExploreStart start_from(const Moment &m, uint32_t max_frames)
{
    ExploreStart s;
    s.state = m.state;
    s.rom = m.rom;
    for (size_t i = 0; i < m.frames.size() && i < max_frames; i++)
        s.pads.push_back(m.frames[i].pads);
    return s;
}

RunScore score_run(const RunRecord &run, const RunRecord &control, const ScoreConfig &cfg)
{
    RunScore s;
    s.crashed = run.crashed;
    s.new_code = run.new_code;
    size_t n = std::min(run.thumbs.size(), control.thumbs.size());
    bool whole = !run.crashed && run.frames >= control.frames && control.frames > 0;
    if (whole)
        s.froze = froze_at(run.hashes, control.hashes, cfg.freeze).has_value();
    s.alive = whole && !s.froze;
    if (n == 0)
        return s;
    // Different: the second half, where what lasts shows.
    double sum = 0;
    size_t from = n / 2;
    for (size_t f = from; f < n; f++)
        sum += differing_share(run.thumbs[f], control.thumbs[f], cfg.threshold);
    s.different = sum / double(n - from);
    // Motion: picture changes at the end, against the control's.
    auto changes = [&](const std::vector<uint64_t> &h) {
        size_t c = 0;
        size_t lo = n > cfg.motion_window ? n - cfg.motion_window : 1;
        for (size_t i = std::max<size_t>(lo, 1); i < n; i++)
            c += h[i] != h[i - 1];
        return c;
    };
    size_t rc = changes(run.hashes), cc = changes(control.hashes);
    s.motion = cc == 0 ? 1.0 : std::min(1.0, double(rc) / double(cc));
    // Detail: colours and edges at the end, against the control's.
    double run_colours = 0, run_edges = 0, ctl_colours = 0, ctl_edges = 0;
    size_t lo = n > cfg.detail_window ? n - cfg.detail_window : 0;
    for (size_t f = lo; f < n; f++)
    {
        auto a = detail_of(run.thumbs[f], cfg.threshold), b = detail_of(control.thumbs[f], cfg.threshold);
        run_colours += a.colours;
        run_edges += a.edges;
        ctl_colours += b.colours;
        ctl_edges += b.edges;
    }
    s.colours_ratio = ctl_colours <= 0 ? 1.0 : run_colours / ctl_colours;
    s.edges_ratio = ctl_edges <= 0 ? 1.0 : run_edges / ctl_edges;
    double end_sum = 0;
    for (size_t f = lo; f < n; f++)
        end_sum += differing_share(run.thumbs[f], control.thumbs[f], cfg.threshold);
    s.end_different = end_sum / double(n - lo);
    // Looks like a game (measured against blind ratings, step 1): about as many edges as the
    // scene, up to twice as many (more is tile garbage; far fewer, a blank screen), and most of
    // its colours.
    double e = s.edges_ratio;
    double edges = e < 1 ? std::min(1.0, e / 0.7) : e <= 2 ? 1.0 : std::max(0.0, 1.0 - (e - 2) / 2);
    double colours = std::min(1.0, s.colours_ratio / 0.6);
    s.detail = edges * colours;
    s.lively = s.motion * s.detail;
    // Visible: past 10% of the picture, how much more differs didn't matter to the ratings.
    double visible = std::min(1.0, s.different / 0.1);
    if (s.alive)
        s.score = visible * s.lively * (1.0 + 0.25 * std::log2(1.0 + s.new_code / 64.0));
    return s;
}

RunRecord play_variant(ExploreRig &rig, const ExploreStart &start, const Recipe *recipe, const PlayOptions &options)
{
    RunRecord out;
    auto &core = rig.core;
    auto &engine = rig.engine;
    auto &rom = rig.rom;
    std::vector<uint8_t> before = core.save_state();
    std::vector<std::pair<uint32_t, uint8_t>> rom_before;
    const auto &clean = rom.clean();
    for (uint32_t i = 0; i < clean.size(); i++)
        if (uint8_t v = rom.read(Target::ROM, i); v != clean[i])
            rom_before.emplace_back(i, v);
    std::vector<uint8_t> flags_before(rig.flags.begin(), rig.flags.end());

    core.begin_unseen();
    engine.hold_starts(true);
    engine.set_active(false);
    rom.restore_clean();
    for (auto [o, v] : start.rom)
        rom.write_at_start(Target::ROM, o, v);
    bool loaded = core.load_state(start.state);
    if (recipe && loaded)
    {
        engine.set_recipe(*recipe);
        engine.set_active(true);
        auto is_rom = [](Target t) { return t == Target::ROM; };
        auto not_rom = [](Target t) { return t != Target::ROM; };
        engine.apply_pins(rom);
        engine.apply_switches(rom);
        engine.apply_changes(rom);
        engine.fire_at_load(rom, is_rom);
        engine.apply_patches(rom, is_rom);
        engine.fire_at_load(rom, not_rom);
        engine.apply_patches(rom, not_rom);
        engine.release();
    }
    engine.hold_starts(false);

    if (loaded)
    {
        CrashDetector detector(rig.detector);
        size_t next_keep = 0;
        out.thumbs.reserve(start.pads.size());
        for (uint32_t f = 0; f < start.pads.size(); f++)
        {
            core.set_pads(start.pads[f]);
            if (options.before_frame)
                options.before_frame(f);
            core.run_unseen_frame(true);
            Picture t;
            core.picture(t, true);
            out.hashes.push_back(picture_hash(t));
            out.thumbs.push_back(std::move(t));
            while (next_keep < options.keep.size() && options.keep[next_keep] < f)
                next_keep++;
            if (next_keep < options.keep.size() && options.keep[next_keep] == f)
            {
                Picture p;
                core.picture(p, false);
                out.kept.push_back(std::move(p));
            }
            out.frames = f + 1;
            if (auto v = detector.observe(f, core.sample()))
            {
                out.crashed = true;
                out.ended = v->text;
                break;
            }
        }
    }
    else
    {
        out.crashed = true;
        out.ended = "the starting state didn't load";
    }
    if (loaded && options.after_frames)
        options.after_frames();

    for (size_t i = 0; i < rig.flags.size() && i < flags_before.size(); i++)
    {
        if ((rig.flags[i] & codeflag::Code) && !(flags_before[i] & codeflag::Code))
            out.new_code++;
        if (!options.keep_learning)
            rig.flags[i] = flags_before[i];
    }
    if (recipe)
    {
        out.firings = engine.status().firings;
        out.writes = engine.event_log().size();
    }
    engine.set_active(false);
    core.end_unseen();
    rom.restore_clean();
    for (auto [o, v] : rom_before)
        rom.write_at_start(Target::ROM, o, v);
    core.load_state(before);
    return out;
}

} // namespace corrupt
