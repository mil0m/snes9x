#include "recipe_json.h"

#include "function_change.h"

#include "nlohmann/json.hpp"

#include <cmath>
#include <cstdio>
#include <regex>
#include <set>

using json = nlohmann::json;
using ojson = nlohmann::ordered_json; // written files keep a fixed, readable key order

namespace corrupt
{

namespace
{

// Sizes for validating without a loaded game.
struct NominalMemory : MemoryAccess
{
    // SRAM and ROM depend on the game: allow the largest, and check against the real one at load.
    size_t size(Target t) const override
    {
        return t == Target::SRAM ? 0x20000 : t == Target::ROM ? 0x800000 : nominal_size(t);
    }
    uint8_t read(Target, uint32_t) const override { return 0; }
    void write(Target, uint32_t, uint8_t) override {}
};

struct Ctx
{
    std::vector<std::string> errors;
    bool ok = true;

    void fail(const std::string &path, const std::string &msg)
    {
        errors.push_back(path + ": " + msg);
        ok = false;
    }
};

std::optional<int64_t> parse_number_string(const std::string &s)
{
    return parse_number(s);
}

// Integers may be JSON numbers, "0x.." hex strings or decimal strings.
std::optional<int64_t> get_int(Ctx &ctx, const json &v, const std::string &path)
{
    if (v.is_number_integer())
        return v.get<int64_t>();
    if (v.is_number_float())
    {
        double d = v.get<double>();
        if (d == double(int64_t(d)))
            return int64_t(d);
    }
    if (v.is_string())
        if (auto n = parse_number_string(v.get<std::string>()))
            return n;
    ctx.fail(path, "expected an integer (number, \"0x..\" or decimal string), got " + v.dump());
    return std::nullopt;
}

std::optional<int64_t> get_int_in(Ctx &ctx, const json &v, const std::string &path, int64_t lo, int64_t hi)
{
    auto n = get_int(ctx, v, path);
    if (n && (*n < lo || *n > hi))
    {
        ctx.fail(path, "must be within " + std::to_string(lo) + ".." + std::to_string(hi) + ", got " + std::to_string(*n));
        return std::nullopt;
    }
    return n;
}

void check_keys(Ctx &ctx, const json &obj, const std::string &path, std::initializer_list<const char *> allowed)
{
    std::set<std::string> ok(allowed.begin(), allowed.end());
    for (auto &[key, _] : obj.items())
        if (!ok.count(key))
            ctx.fail(path, "unknown key '" + key + "'");
}

bool require_object(Ctx &ctx, const json &v, const std::string &path)
{
    if (!v.is_object())
    {
        ctx.fail(path, "expected an object");
        return false;
    }
    return true;
}

std::optional<Target> get_target(Ctx &ctx, const json &v, const std::string &path)
{
    if (v.is_string())
        if (auto t = parse_target(v.get<std::string>()))
            return t;
    ctx.fail(path, "unknown target " + v.dump() + " (expected wram, sram, vram, oam, cgram or aram)");
    return std::nullopt;
}

// A range is ["first", "last"], a region name, or absent (= the whole target).
// Returns the range and, when it came from a region, the region's name.
std::optional<std::pair<AddressRange, std::string>>
get_range(Ctx &ctx, const json &obj, const std::string &path, std::optional<Target> target, const Regions &regions)
{
    std::string rpath = path + ".range";

    if (!obj.contains("range"))
    {
        if (!target)
        {
            ctx.fail(path, "needs a target or a named range");
            return std::nullopt;
        }
        if (*target == Target::SRAM || *target == Target::ROM)
        {
            ctx.fail(rpath, std::string(target_name(*target)) + " rules need an explicit range (its size depends on the game)");
            return std::nullopt;
        }
        return std::pair{ AddressRange{ *target, 0, uint32_t(nominal_size(*target) - 1) }, std::string() };
    }

    const json &r = obj["range"];
    if (r.is_string())
    {
        auto name = r.get<std::string>();
        auto it = regions.find(name);
        if (it == regions.end())
        {
            ctx.fail(rpath, "unknown region '" + name + "' (not in this game's regions.json)");
            return std::nullopt;
        }
        if (target && *target != it->second.target)
        {
            ctx.fail(rpath, "region '" + name + "' is in " + target_name(it->second.target) + ", not " + target_name(*target));
            return std::nullopt;
        }
        return std::pair{ it->second, name };
    }

    if (!r.is_array() || r.size() != 2)
    {
        ctx.fail(rpath, "expected [first, last] or a region name");
        return std::nullopt;
    }
    if (!target)
    {
        ctx.fail(path, "needs a target");
        return std::nullopt;
    }
    auto first = get_int_in(ctx, r[0], rpath + "[0]", 0, 0xFFFFFF);
    auto last = get_int_in(ctx, r[1], rpath + "[1]", 0, 0xFFFFFF);
    if (!first || !last)
        return std::nullopt;
    return std::pair{ AddressRange{ *target, uint32_t(*first), uint32_t(*last) }, std::string() };
}

void parse_op_json(Ctx &ctx, const json &o, const std::string &path, Op &op, const Regions &regions)
{
    if (!require_object(ctx, o, path))
        return;
    check_keys(ctx, o, path, { "type", "min", "max", "mode", "bits", "mask", "width", "a", "b", "from", "length", "value",
                               "window", "dx", "dy", "wrap", "axis", "curve", "amount", "kernel", "reach", "mutual", "pick",
                               "put_back" });

    if (!o.contains("type") || !o["type"].is_string() || !parse_op(o["type"].get<std::string>()))
    {
        ctx.fail(path + ".type", "expected one of add, random, bitflip, swap, copy, freeze, shuffle, shift, flip, "
                                 "levels, filter, stamp, fill, opcode_swap, branch_flip, nop_out, pointer_swap, kill");
        return;
    }
    op.type = *parse_op(o["type"].get<std::string>());

    // Useful defaults where a missing value would make the op do nothing.
    if (op.type == OpType::Random)
        op.max = 255;
    if (op.type == OpType::Add)
    {
        op.min = -1;
        op.max = 1;
    }

    auto int_field = [&](const char *key, auto &out, int64_t lo, int64_t hi) {
        if (o.contains(key))
            if (auto n = get_int_in(ctx, o[key], path + "." + key, lo, hi))
                out = decltype(out)(*n);
    };
    auto opt_field = [&](const char *key, auto &out, int64_t lo, int64_t hi) {
        if (o.contains(key))
            if (auto n = get_int_in(ctx, o[key], path + "." + key, lo, hi))
                out = typename std::remove_reference_t<decltype(out)>::value_type(*n);
    };

    int_field("min", op.min, -1000, 1000);
    int_field("max", op.max, -1000, 1000);
    int_field("bits", op.bits, -1000, 1000);
    int_field("width", op.width, -1000, 1000);
    int_field("window", op.window, -1000, 1000);
    int_field("length", op.length, 0, 0x20000);
    opt_field("mask", op.mask, 0, 255);
    opt_field("value", op.value, 0, 255);
    opt_field("a", op.a, 0, 0xFFFFFF);
    opt_field("b", op.b, 0, 0xFFFFFF);
    int_field("dx", op.dx, -0xFFFF, 0xFFFF);
    int_field("dy", op.dy, -0xFFFF, 0xFFFF);
    if (o.contains("reach"))
    {
        auto reach = o["reach"].is_string() ? o["reach"].get<std::string>() : "";
        if (reach == "likely")
            op.likely = true;
        else if (reach != "seen")
            ctx.fail(path + ".reach", "expected \"seen\" or \"likely\"");
    }
    if (o.contains("pick"))
    {
        auto pick = o["pick"].is_string() ? o["pick"].get<std::string>() : "";
        if (pick == "any")
            op.any = true;
        else if (pick != "busiest")
            ctx.fail(path + ".pick", "expected \"busiest\" or \"any\"");
    }
    if (o.contains("put_back"))
    {
        if (auto n = get_int_in(ctx, o["put_back"], path + ".put_back", 0, 3600000))
            op.put_back = uint32_t(*n);
    }
    if (o.contains("mutual"))
    {
        if (o["mutual"].is_boolean())
            op.mutual = o["mutual"].get<bool>();
        else
            ctx.fail(path + ".mutual", "expected true or false");
    }
    if (o.contains("wrap"))
    {
        if (o["wrap"].is_boolean())
            op.wrap = o["wrap"].get<bool>();
        else
            ctx.fail(path + ".wrap", "expected true or false");
    }
    auto named = [&](const char *key, auto parse, auto &out, const char *expected) {
        if (!o.contains(key))
            return;
        auto v = o[key].is_string() ? parse(o[key].get<std::string>()) : std::nullopt;
        if (v)
            out = *v;
        else
            ctx.fail(path + "." + key, std::string("expected one of ") + expected);
    };
    named("axis", parse_axis, op.axis, "horizontal, vertical, both");
    named("curve", parse_curve, op.curve, "invert, posterize, threshold, gamma, contrast");
    named("kernel", parse_kernel, op.kernel, "blur, sharpen, median, emboss, edges, max, min");
    if (o.contains("amount"))
    {
        if (o["amount"].is_number())
            op.amount = o["amount"].get<double>();
        else
            ctx.fail(path + ".amount", "expected a number");
    }
    else if (op.type == OpType::Levels) // a missing amount would make most curves do nothing
        op.amount = op.curve == Curve::Posterize ? 4 : op.curve == Curve::Threshold ? 128 : op.curve == Curve::Gamma ? 1.5 : 0.5;

    if (o.contains("mode"))
    {
        auto mode = o["mode"].is_string() ? o["mode"].get<std::string>() : "";
        if (mode == "clamp")
            op.clamp = true;
        else if (mode != "wrap")
            ctx.fail(path + ".mode", "expected \"wrap\" or \"clamp\"");
    }

    if (o.contains("from") && op.type == OpType::Stamp)
        int_field("from", op.source, 0, 0xFFFFFF); // the top-left of the region it copies
    else if (o.contains("from"))
    {
        const json &f = o["from"];
        std::string fpath = path + ".from";
        if (f.is_string())
        {
            json wrapper = { { "range", f } };
            if (auto r = get_range(ctx, wrapper, fpath, std::nullopt, regions))
                op.from = r->first;
        }
        else if (require_object(ctx, f, fpath))
        {
            check_keys(ctx, f, fpath, { "target", "range" });
            std::optional<Target> t;
            if (f.contains("target"))
                t = get_target(ctx, f["target"], fpath + ".target");
            if (auto r = get_range(ctx, f, fpath, t, regions))
                op.from = r->first;
        }
    }
}

void parse_when(Ctx &ctx, const json &w, const std::string &path, Trigger &t)
{
    if (!require_object(ctx, w, path))
        return;
    check_keys(ctx, w, path, { "once_at_frame", "every_frames", "chance_per_frame", "hotkey", "at_load", "window" });

    int kinds = 0;
    for (auto key : { "once_at_frame", "every_frames", "chance_per_frame", "hotkey", "at_load" })
        kinds += w.contains(key);
    if (kinds != 1)
    {
        ctx.fail(path, "needs exactly one of once_at_frame, every_frames, chance_per_frame, hotkey, at_load");
        return;
    }

    if (w.contains("at_load"))
    {
        t.type = TriggerType::AtLoad;
        if (!w["at_load"].is_boolean() || !w["at_load"].get<bool>())
            ctx.fail(path + ".at_load", "expected true");
    }
    else if (w.contains("once_at_frame"))
    {
        t.type = TriggerType::OnceAtFrame;
        if (auto n = get_int_in(ctx, w["once_at_frame"], path + ".once_at_frame", 0, INT64_MAX))
            t.frame = uint64_t(*n);
    }
    else if (w.contains("every_frames"))
    {
        t.type = TriggerType::EveryFrames;
        if (auto n = get_int_in(ctx, w["every_frames"], path + ".every_frames", 0, UINT32_MAX))
            t.every = uint32_t(*n);
    }
    else if (w.contains("chance_per_frame"))
    {
        t.type = TriggerType::ChancePerFrame;
        if (w["chance_per_frame"].is_number())
            t.chance = w["chance_per_frame"].get<double>();
        else
            ctx.fail(path + ".chance_per_frame", "expected a number");
    }
    else
    {
        t.type = TriggerType::Hotkey;
        if (auto n = get_int(ctx, w["hotkey"], path + ".hotkey"))
            t.hotkey = int(*n);
    }

    if (w.contains("window"))
    {
        const json &win = w["window"];
        if (!win.is_array() || win.size() != 2)
            ctx.fail(path + ".window", "expected [start, end]");
        else
        {
            auto s = get_int_in(ctx, win[0], path + ".window[0]", 0, INT64_MAX);
            auto e = get_int_in(ctx, win[1], path + ".window[1]", 0, INT64_MAX);
            if (s && e)
                t.window = FrameWindow{ uint64_t(*s), uint64_t(*e) };
        }
    }
}

std::optional<Rule> parse_rule(Ctx &ctx, const json &r, size_t index, const Regions &regions)
{
    std::string path = "rules[" + std::to_string(index) + "]";
    if (!require_object(ctx, r, path))
        return std::nullopt;

    size_t errors_before = ctx.errors.size();
    check_keys(ctx, r, path, { "name", "target", "range", "rect", "op", "when", "count", "pick", "enabled", "intensity",
                               "avoid_hot", "salt", "role", "classes", "except", "within" });

    Rule rule;
    rule.name = r.contains("name") && r["name"].is_string() ? r["name"].get<std::string>() : "rule " + std::to_string(index + 1);

    std::optional<Target> target;
    if (r.contains("target"))
    {
        target = get_target(ctx, r["target"], path + ".target");
        if (!target)
            return std::nullopt;
    }
    std::optional<std::pair<AddressRange, std::string>> range;
    if (r.contains("rect"))
    {
        const json &rc = r["rect"];
        std::string rpath = path + ".rect";
        if (r.contains("range"))
            ctx.fail(path, "has both a range and a rect; use one");
        else if (!target)
            ctx.fail(path, "a rect needs a target");
        else if (require_object(ctx, rc, rpath))
        {
            check_keys(ctx, rc, rpath, { "start", "row", "width", "height" });
            std::optional<int64_t> v[4];
            const char *keys[4] = { "start", "row", "width", "height" };
            for (int i = 0; i < 4; i++)
                if (!rc.contains(keys[i]))
                    ctx.fail(rpath, std::string("needs ") + keys[i]);
                else
                    v[i] = get_int_in(ctx, rc[keys[i]], rpath + "." + keys[i], 0, 0xFFFFFF);
            if (v[0] && v[1] && v[2] && v[3])
            {
                set_rect(rule, *target, uint32_t(*v[0]), Rect{ uint32_t(*v[1]), uint32_t(*v[2]), uint32_t(*v[3]) });
                range = std::pair{ rule.range, std::string() };
            }
        }
    }
    else
    {
        range = get_range(ctx, r, path, target, regions);
        if (range)
        {
            rule.range = range->first;
            rule.region = range->second;
        }
    }

    if (r.contains("op"))
        parse_op_json(ctx, r["op"], path + ".op", rule.op, regions);
    else
        ctx.fail(path, "missing 'op'");

    if (r.contains("when"))
        parse_when(ctx, r["when"], path + ".when", rule.when);
    else
        ctx.fail(path, "missing 'when'");

    if (r.contains("count"))
        if (auto n = get_int_in(ctx, r["count"], path + ".count", 0, 0x20000))
            rule.count = uint32_t(*n);

    if (r.contains("pick"))
    {
        auto p = r["pick"].is_string() ? r["pick"].get<std::string>() : "";
        if (p == "sticky")
            rule.pick = Pick::Sticky;
        else if (p != "fresh")
            ctx.fail(path + ".pick", "expected \"fresh\" or \"sticky\"");
    }

    if (r.contains("role"))
    {
        auto role = r["role"].is_string() ? parse_role(r["role"].get<std::string>()) : std::nullopt;
        if (role)
            rule.aim.role = *role;
        else
            ctx.fail(path + ".role", "expected one of any, opcode, operand, code, data, dma, used");
    }
    if (r.contains("within"))
    {
        if (r["within"].is_string() && r["within"].get<std::string>() == "busy_functions")
            rule.aim.in_busy_functions = true;
        else
            ctx.fail(path + ".within", "expected \"busy_functions\"");
    }
    for (auto [key, out] : { std::pair{ "classes", &rule.aim.classes }, std::pair{ "except", &rule.aim.except } })
        if (r.contains(key))
        {
            const json &list = r[key];
            if (!list.is_array())
                ctx.fail(path + "." + key, "expected a list of instruction classes");
            else
                for (auto &c : list)
                {
                    auto cls = c.is_string() ? class_from_name(c.get<std::string>()) : std::nullopt;
                    if (cls)
                        out->push_back(*cls);
                    else
                        ctx.fail(path + "." + key, "unknown class " + c.dump() +
                                                       "; expected arith, logic, shift, compare, load, store, transfer, "
                                                       "branch, jump, return, stack, flags, width, block or misc");
                }
        }

    for (auto [key, out] : { std::pair{ "enabled", &rule.enabled }, std::pair{ "avoid_hot", &rule.avoid_hot } })
        if (r.contains(key))
        {
            if (r[key].is_boolean())
                *out = r[key].get<bool>();
            else
                ctx.fail(path + "." + key, "expected true or false");
        }

    if (r.contains("salt"))
    {
        const json &s = r["salt"];
        if (s.is_number_unsigned())
            rule.salt = s.get<uint64_t>();
        else if (auto n = get_int(ctx, s, path + ".salt"))
            rule.salt = uint64_t(*n);
    }

    if (r.contains("intensity"))
    {
        if (r["intensity"].is_number())
            rule.intensity = r["intensity"].get<double>();
        else
            ctx.fail(path + ".intensity", "expected a number");
    }

    if (ctx.errors.size() != errors_before || !range)
        return std::nullopt;

    NominalMemory nominal;
    if (auto err = validate_rule(rule, nominal))
    {
        ctx.fail(path + "." + err->substr(0, err->find(':')), err->substr(err->find(':') + 2));
        return std::nullopt;
    }
    return rule;
}

std::string hex_addr(uint32_t v)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%04X", v);
    return buf;
}

std::string hex_byte(uint8_t v)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "0x%02X", v);
    return buf;
}

ojson range_json(const AddressRange &r)
{
    return ojson::array({ hex_addr(r.first), hex_addr(r.last) });
}

ojson op_json(const Op &op)
{
    ojson o = { { "type", op_name(op.type) } };
    switch (op.type)
    {
    case OpType::Add:
        o["min"] = op.min;
        o["max"] = op.max;
        if (op.clamp)
            o["mode"] = "clamp";
        break;
    case OpType::Random:
        o["min"] = op.min;
        o["max"] = op.max;
        break;
    case OpType::Bitflip:
        if (op.mask)
            o["mask"] = hex_byte(*op.mask);
        else
            o["bits"] = op.bits;
        break;
    case OpType::Swap:
        if (op.width != 1)
            o["width"] = op.width;
        if (op.a)
            o["a"] = hex_addr(*op.a);
        if (op.b)
            o["b"] = hex_addr(*op.b);
        break;
    case OpType::Copy:
        if (op.from)
            o["from"] = { { "target", target_name(op.from->target) }, { "range", range_json(*op.from) } };
        o["length"] = op.length;
        break;
    case OpType::Freeze:
        if (op.value)
            o["value"] = hex_byte(*op.value);
        break;
    case OpType::Shuffle:
        o["window"] = op.window;
        break;
    case OpType::Shift:
        o["dx"] = op.dx;
        o["dy"] = op.dy;
        if (!op.wrap)
        {
            o["wrap"] = false;
            if (op.value)
                o["value"] = hex_byte(*op.value);
        }
        break;
    case OpType::Flip:
        o["axis"] = axis_name(op.axis);
        break;
    case OpType::Levels:
        o["curve"] = curve_name(op.curve);
        if (op.curve != Curve::Invert)
            o["amount"] = op.amount;
        break;
    case OpType::Filter:
        o["kernel"] = kernel_name(op.kernel);
        break;
    case OpType::Stamp:
        o["from"] = hex_addr(op.source);
        break;
    case OpType::Fill:
        o["value"] = hex_byte(op.value.value_or(0));
        break;
    case OpType::OpcodeSwap:
    case OpType::BranchFlip:
    case OpType::NopOut:
        break;
    case OpType::PointerSwap:
        if (op.likely)
            o["reach"] = "likely";
        if (op.mutual)
            o["mutual"] = true;
        break;
    case OpType::Kill:
        if (op.any)
            o["pick"] = "any";
        if (op.put_back)
            o["put_back"] = op.put_back;
        break;
    }
    return o;
}

ojson when_json(const Trigger &t)
{
    ojson w = ojson::object();
    switch (t.type)
    {
    case TriggerType::OnceAtFrame: w["once_at_frame"] = t.frame; break;
    case TriggerType::EveryFrames: w["every_frames"] = t.every; break;
    case TriggerType::ChancePerFrame: w["chance_per_frame"] = t.chance; break;
    case TriggerType::Hotkey: w["hotkey"] = t.hotkey; break;
    case TriggerType::AtLoad: w["at_load"] = true; break;
    }
    if (t.window)
        w["window"] = ojson::array({ t.window->start, t.window->end });
    return w;
}

// dump(2) puts every array element on its own line; short scalar arrays like
// ranges and windows read better as [ "0x0000", "0x01FF" ].
std::string dump_compact(const ojson &doc)
{
    static const std::regex short_array(R"(\[\n\s+((?:"[^"\n]*"|-?[0-9.]+))(?:,\n\s+((?:"[^"\n]*"|-?[0-9.]+)))\n\s+\])");
    return std::regex_replace(doc.dump(2), short_array, "[ $1, $2 ]") + "\n";
}

// Detection times are seconds in JSON (0 switches a signal off), frames inside.
DetectorConfig parse_detection(Ctx &ctx, const json &d)
{
    DetectorConfig cfg;
    const std::string path = "recipe.crash_detection";
    if (!require_object(ctx, d, path))
        return cfg;
    check_keys(ctx, d, path, { "enabled", "cpu_stopped", "runaway_frames", "nmi_off_seconds", "frozen_seconds", "black_seconds", "min_presses", "reboot" });

    for (auto [key, out] : { std::pair{ "enabled", &cfg.enabled }, std::pair{ "cpu_stopped", &cfg.cpu_stopped },
                             std::pair{ "reboot", &cfg.reboot } })
        if (d.contains(key))
        {
            if (d[key].is_boolean())
                *out = d[key].get<bool>();
            else
                ctx.fail(path + "." + key, "expected true or false");
        }

    if (d.contains("runaway_frames"))
        if (auto n = get_int_in(ctx, d["runaway_frames"], path + ".runaway_frames", 0, 100000))
            cfg.runaway_frames = uint32_t(*n);
    if (d.contains("min_presses"))
        if (auto n = get_int_in(ctx, d["min_presses"], path + ".min_presses", 0, 1000))
            cfg.min_presses = uint32_t(*n);

    for (auto [key, out] : { std::pair{ "nmi_off_seconds", &cfg.nmi_off_frames },
                             std::pair{ "frozen_seconds", &cfg.frozen_frames },
                             std::pair{ "black_seconds", &cfg.black_frames } })
        if (d.contains(key))
        {
            if (!d[key].is_number() || d[key].get<double>() < 0 || d[key].get<double>() > 3600)
                ctx.fail(path + "." + key, "expected seconds within 0..3600 (0 switches it off)");
            else
                *out = uint32_t(std::lround(d[key].get<double>() * 60.0));
        }
    return cfg;
}

ojson detection_json(const DetectorConfig &cfg)
{
    ojson d;
    d["enabled"] = cfg.enabled;
    d["cpu_stopped"] = cfg.cpu_stopped;
    d["runaway_frames"] = cfg.runaway_frames;
    d["nmi_off_seconds"] = cfg.nmi_off_frames / 60.0;
    d["frozen_seconds"] = cfg.frozen_frames / 60.0;
    d["black_seconds"] = cfg.black_frames / 60.0;
    d["min_presses"] = cfg.min_presses;
    d["reboot"] = cfg.reboot;
    return d;
}

} // namespace

std::optional<int64_t> parse_number(std::string_view text)
{
    auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!text.empty() && is_space(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && is_space(text.back()))
        text.remove_suffix(1);

    bool negative = false;
    if (!text.empty() && text.front() == '-')
    {
        negative = true;
        text.remove_prefix(1);
    }
    int base = 10;
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        base = 16;
        text.remove_prefix(2);
    }
    else if (!text.empty() && text.front() == '$')
    {
        base = 16;
        text.remove_prefix(1);
    }
    if (text.empty())
        return std::nullopt;

    uint64_t value = 0;
    for (char c : text)
    {
        int digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else
            return std::nullopt;
        if (value > (uint64_t(INT64_MAX) - uint64_t(digit)) / uint64_t(base))
            return std::nullopt;
        value = value * uint64_t(base) + uint64_t(digit);
    }
    return negative ? -int64_t(value) : int64_t(value);
}

ParseResult parse_recipe(std::string_view json_text, const Regions &regions)
{
    ParseResult result;
    Ctx ctx;

    json doc;
    try
    {
        doc = json::parse(json_text);
    }
    catch (const json::parse_error &e)
    {
        result.errors.push_back(std::string("not valid JSON: ") + e.what());
        return result;
    }

    if (!require_object(ctx, doc, "recipe"))
    {
        result.errors = ctx.errors;
        return result;
    }
    check_keys(ctx, doc, "recipe", { "format", "name", "notes", "game", "seed", "intensity", "anchor_state", "rules",
                                     "patches", "pins", "switches", "changes", "protect", "crash_policy", "rom_policy", "instant_revert", "test_first",
                                     "crash_detection" });

    if (!doc.contains("format") || !doc["format"].is_number_integer() || doc["format"].get<int>() != Recipe::kFormat)
    {
        ctx.fail("recipe.format", "this Snes9x reads format " + std::to_string(Recipe::kFormat) +
                                      ", the file says " + (doc.contains("format") ? doc["format"].dump() : "nothing"));
        result.errors = ctx.errors;
        return result;
    }

    Recipe recipe;
    auto str = [&](const json &obj, const char *key, std::string &out, const std::string &path) {
        if (!obj.contains(key))
            return;
        if (obj[key].is_string())
            out = obj[key].get<std::string>();
        else
            ctx.fail(path + "." + key, "expected a string");
    };
    str(doc, "name", recipe.name, "recipe");
    str(doc, "notes", recipe.notes, "recipe");
    str(doc, "anchor_state", recipe.anchor_state, "recipe");

    if (doc.contains("game") && require_object(ctx, doc["game"], "recipe.game"))
    {
        check_keys(ctx, doc["game"], "recipe.game", { "title", "sha256", "crc32" });
        str(doc["game"], "title", recipe.game.title, "recipe.game");
        str(doc["game"], "sha256", recipe.game.sha256, "recipe.game");
        str(doc["game"], "crc32", recipe.game.crc32, "recipe.game");
    }

    if (doc.contains("seed"))
    {
        const json &s = doc["seed"];
        if (s.is_number_unsigned())
            recipe.seed = s.get<uint64_t>();
        else if (auto n = get_int(ctx, s, "recipe.seed"))
            recipe.seed = uint64_t(*n);
    }

    if (doc.contains("intensity"))
    {
        const json &v = doc["intensity"];
        if (v.is_number() && v.get<double>() >= 0.0 && v.get<double>() <= 1.0)
            recipe.intensity = v.get<double>();
        else
            ctx.fail("recipe.intensity", "expected a number within 0..1");
    }

    if (doc.contains("patches"))
    {
        const json &ps = doc["patches"];
        if (!ps.is_array())
            ctx.fail("recipe.patches", "expected a list");
        else
        {
            NominalMemory nominal;
            for (size_t i = 0; i < ps.size(); i++)
            {
                std::string path = "patches[" + std::to_string(i) + "]";
                const json &p = ps[i];
                if (!require_object(ctx, p, path))
                    continue;
                check_keys(ctx, p, path, { "target", "at", "bytes" });
                std::optional<Target> t = p.contains("target") ? get_target(ctx, p["target"], path + ".target") : std::nullopt;
                std::optional<int64_t> at = p.contains("at") ? get_int_in(ctx, p["at"], path + ".at", 0, 0xFFFFFF) : std::nullopt;
                std::string hexs = p.contains("bytes") && p["bytes"].is_string() ? p["bytes"].get<std::string>() : "";
                std::vector<uint8_t> bytes;
                bool good = !hexs.empty() && hexs.size() % 2 == 0;
                for (size_t k = 0; good && k < hexs.size(); k += 2)
                {
                    char *end = nullptr;
                    std::string two = hexs.substr(k, 2);
                    long v = std::strtol(two.c_str(), &end, 16);
                    good = end == two.c_str() + 2;
                    bytes.push_back(uint8_t(v));
                }
                if (!t || !at)
                {
                    ctx.fail(path, "needs a target and at");
                    continue;
                }
                if (!good)
                {
                    ctx.fail(path + ".bytes", "expected pairs of hex digits, like \"EA00FF\"");
                    continue;
                }
                AddressRange span{ *t, uint32_t(*at), uint32_t(*at + int64_t(bytes.size()) - 1) };
                if (auto err = validate_range(span, nominal))
                {
                    ctx.fail(path, *err);
                    continue;
                }
                std::vector<std::pair<uint32_t, uint8_t>> pairs;
                for (uint32_t k = 0; k < bytes.size(); k++)
                    pairs.push_back({ uint32_t(*at) + k, bytes[k] });
                add_patch_bytes(recipe.patches, *t, pairs);
            }
        }
    }

    if (doc.contains("pins"))
    {
        const json &ps = doc["pins"];
        if (!ps.is_array())
            ctx.fail("recipe.pins", "expected a list");
        else
            for (size_t i = 0; i < ps.size(); i++)
            {
                std::string path = "pins[" + std::to_string(i) + "]";
                const json &p = ps[i];
                if (!require_object(ctx, p, path))
                    continue;
                check_keys(ctx, p, path, { "rom", "value", "was", "note" });
                if (!p.contains("rom") || !p.contains("value") || !p.contains("was"))
                {
                    ctx.fail(path, "needs rom (the offset), value and was (the clean value)");
                    continue;
                }
                auto offset = get_int_in(ctx, p["rom"], path + ".rom", 0, 0xFFFFFF);
                auto value = get_int_in(ctx, p["value"], path + ".value", 0, 255);
                auto was = get_int_in(ctx, p["was"], path + ".was", 0, 255);
                if (!offset || !value || !was)
                    continue;
                Pin pin{ uint32_t(*offset), uint8_t(*value), uint8_t(*was), {} };
                if (p.contains("note") && p["note"].is_string())
                    pin.note = p["note"].get<std::string>();
                recipe.pins.push_back(pin);
            }
    }

    if (doc.contains("switches"))
    {
        const json &ss = doc["switches"];
        if (!ss.is_array())
            ctx.fail("recipe.switches", "expected a list");
        else
            for (size_t i = 0; i < ss.size(); i++)
            {
                std::string path = "switches[" + std::to_string(i) + "]";
                const json &j = ss[i];
                if (!require_object(ctx, j, path))
                    continue;
                check_keys(ctx, j, path, { "entry", "return", "was", "hotkey", "start_off", "note", "every", "for" });
                if (!j.contains("entry") || !j.contains("return") || !j.contains("was"))
                {
                    ctx.fail(path, "needs entry (the offset), return (0x60 or 0x6B) and was (the clean value)");
                    continue;
                }
                auto entry = get_int_in(ctx, j["entry"], path + ".entry", 0, 0xFFFFFF);
                auto ret = get_int_in(ctx, j["return"], path + ".return", 0, 255);
                auto was = get_int_in(ctx, j["was"], path + ".was", 0, 255);
                if (!entry || !ret || !was)
                    continue;
                if (*ret != 0x60 && *ret != 0x6B)
                {
                    ctx.fail(path + ".return", "expected 0x60 (RTS) or 0x6B (RTL)");
                    continue;
                }
                Switch s{ uint32_t(*entry), uint8_t(*ret), uint8_t(*was), 0, false, {} };
                if (j.contains("hotkey"))
                {
                    auto key = get_int_in(ctx, j["hotkey"], path + ".hotkey", 0, 9);
                    if (!key)
                        continue;
                    s.hotkey = int(*key);
                }
                if (j.contains("start_off"))
                {
                    if (!j["start_off"].is_boolean())
                    {
                        ctx.fail(path + ".start_off", "expected true or false");
                        continue;
                    }
                    s.start_off = j["start_off"].get<bool>();
                }
                if (j.contains("note") && j["note"].is_string())
                    s.note = j["note"].get<std::string>();
                if (j.contains("every") || j.contains("for"))
                {
                    auto every = j.contains("every") ? get_int_in(ctx, j["every"], path + ".every", 2, 3600000) : std::nullopt;
                    auto for_frames = j.contains("for") ? get_int_in(ctx, j["for"], path + ".for", 1, 3600000) : std::nullopt;
                    if (!every || !for_frames)
                    {
                        if (!j.contains("every") || !j.contains("for"))
                            ctx.fail(path, "a timer needs both every and for (frames)");
                        continue;
                    }
                    if (*for_frames >= *every)
                    {
                        ctx.fail(path + ".for", "must be shorter than every");
                        continue;
                    }
                    s.every = uint32_t(*every);
                    s.for_frames = uint32_t(*for_frames);
                }
                recipe.switches.push_back(s);
            }
    }

    if (doc.contains("changes"))
    {
        const json &cs = doc["changes"];
        if (!cs.is_array())
            ctx.fail("recipe.changes", "expected a list");
        else
            for (size_t i = 0; i < cs.size(); i++)
            {
                std::string path = "changes[" + std::to_string(i) + "]";
                const json &j = cs[i];
                if (!require_object(ctx, j, path))
                    continue;
                check_keys(ctx, j, path, { "function", "at", "kind", "value", "was", "hotkey", "start_on", "note", "every", "for" });
                if (!j.contains("function") || !j.contains("at") || !j.contains("kind") || !j.contains("value") ||
                    !j.contains("was"))
                {
                    ctx.fail(path, "needs function, at, kind, value and was");
                    continue;
                }
                auto function = get_int_in(ctx, j["function"], path + ".function", 0, 0xFFFFFF);
                auto at = get_int_in(ctx, j["at"], path + ".at", 0, 0xFFFFFF);
                if (!function || !at)
                    continue;
                auto kind = j["kind"].is_string() ? parse_change_kind(j["kind"].get<std::string>()) : std::nullopt;
                if (!kind)
                {
                    ctx.fail(path + ".kind", "expected number, branch, math, skip or redirect");
                    continue;
                }
                auto hex_bytes = [&](const char *key) -> std::optional<std::vector<uint8_t>> {
                    std::string hexs = j[key].is_string() ? j[key].get<std::string>() : "";
                    std::vector<uint8_t> bytes;
                    bool good = !hexs.empty() && hexs.size() % 2 == 0 && hexs.size() <= 8;
                    for (size_t k = 0; good && k < hexs.size(); k += 2)
                    {
                        char *end = nullptr;
                        std::string two = hexs.substr(k, 2);
                        long v = std::strtol(two.c_str(), &end, 16);
                        good = end == two.c_str() + 2;
                        bytes.push_back(uint8_t(v));
                    }
                    if (!good)
                    {
                        ctx.fail(path + "." + key, "expected 1 to 4 bytes as pairs of hex digits, like \"6903\"");
                        return std::nullopt;
                    }
                    return bytes;
                };
                auto value = hex_bytes("value");
                auto was = hex_bytes("was");
                if (!value || !was)
                    continue;
                if (value->size() != was->size())
                {
                    ctx.fail(path, "value and was need the same number of bytes");
                    continue;
                }
                Change c;
                c.function = uint32_t(*function);
                c.at = uint32_t(*at);
                c.kind = *kind;
                c.value = *value;
                c.was = *was;
                if (j.contains("hotkey"))
                {
                    auto key = get_int_in(ctx, j["hotkey"], path + ".hotkey", 0, 9);
                    if (!key)
                        continue;
                    c.hotkey = int(*key);
                }
                if (j.contains("start_on"))
                {
                    if (!j["start_on"].is_boolean())
                    {
                        ctx.fail(path + ".start_on", "expected true or false");
                        continue;
                    }
                    c.start_on = j["start_on"].get<bool>();
                }
                if (j.contains("note") && j["note"].is_string())
                    c.note = j["note"].get<std::string>();
                if (j.contains("every") || j.contains("for"))
                {
                    auto every = j.contains("every") ? get_int_in(ctx, j["every"], path + ".every", 2, 3600000) : std::nullopt;
                    auto for_frames = j.contains("for") ? get_int_in(ctx, j["for"], path + ".for", 1, 3600000) : std::nullopt;
                    if (!every || !for_frames)
                    {
                        if (!j.contains("every") || !j.contains("for"))
                            ctx.fail(path, "a timer needs both every and for (frames)");
                        continue;
                    }
                    if (*for_frames >= *every)
                    {
                        ctx.fail(path + ".for", "must be shorter than every");
                        continue;
                    }
                    c.every = uint32_t(*every);
                    c.for_frames = uint32_t(*for_frames);
                }
                recipe.changes.push_back(c);
            }
    }

    if (doc.contains("protect"))
    {
        const json &ps = doc["protect"];
        if (!ps.is_array())
            ctx.fail("recipe.protect", "expected a list");
        else
            for (size_t i = 0; i < ps.size(); i++)
            {
                std::string path = "protect[" + std::to_string(i) + "]";
                const json &j = ps[i];
                if (!require_object(ctx, j, path))
                    continue;
                check_keys(ctx, j, path, { "entry", "deep" });
                if (!j.contains("entry"))
                {
                    ctx.fail(path, "needs entry (the function's offset)");
                    continue;
                }
                auto entry = get_int_in(ctx, j["entry"], path + ".entry", 0, 0xFFFFFF);
                if (!entry)
                    continue;
                Protect p{ uint32_t(*entry), false };
                if (j.contains("deep"))
                {
                    if (!j["deep"].is_boolean())
                    {
                        ctx.fail(path + ".deep", "expected true or false");
                        continue;
                    }
                    p.deep = j["deep"].get<bool>();
                }
                recipe.protects.push_back(p);
            }
    }

    if (doc.contains("crash_policy"))
    {
        const json &p = doc["crash_policy"];
        if (p.is_string() && parse_policy(p.get<std::string>()))
            recipe.crash_policy = parse_policy(p.get<std::string>());
        else
            ctx.fail("recipe.crash_policy", "expected one of reroll, skip, dampen, disable, escalate");
    }

    if (doc.contains("rom_policy"))
    {
        const json &p = doc["rom_policy"];
        if (p.is_string() && parse_rom_policy(p.get<std::string>()))
            recipe.rom_policy = parse_rom_policy(p.get<std::string>());
        else
            ctx.fail("recipe.rom_policy", "expected revert or reroll");
    }

    if (doc.contains("instant_revert"))
    {
        if (doc["instant_revert"].is_boolean())
            recipe.instant_revert = doc["instant_revert"].get<bool>();
        else
            ctx.fail("recipe.instant_revert", "expected true or false");
    }

    if (doc.contains("test_first"))
    {
        const json &t = doc["test_first"];
        if (t.is_number() && t.get<double>() >= 0 && t.get<double>() <= 30)
            recipe.test_first = t.get<double>();
        else
            ctx.fail("recipe.test_first", "expected seconds from 0 (off) to 30");
    }

    if (doc.contains("crash_detection"))
        recipe.crash_detection = parse_detection(ctx, doc["crash_detection"]);

    if (doc.contains("rules"))
    {
        if (!doc["rules"].is_array())
            ctx.fail("recipe.rules", "expected a list");
        else
            for (size_t i = 0; i < doc["rules"].size(); i++)
                if (auto rule = parse_rule(ctx, doc["rules"][i], i, regions))
                    recipe.rules.push_back(*rule);
    }

    result.errors = ctx.errors;
    if (result.errors.empty())
        result.recipe = std::move(recipe);
    return result;
}

std::string write_recipe(const Recipe &recipe)
{
    ojson doc;
    doc["format"] = Recipe::kFormat;
    doc["name"] = recipe.name;
    if (!recipe.notes.empty())
        doc["notes"] = recipe.notes;
    if (!recipe.game.title.empty() || !recipe.game.sha256.empty() || !recipe.game.crc32.empty())
        doc["game"] = { { "title", recipe.game.title }, { "sha256", recipe.game.sha256 }, { "crc32", recipe.game.crc32 } };
    doc["seed"] = recipe.seed;
    if (recipe.intensity != 1.0)
        doc["intensity"] = recipe.intensity;
    if (!recipe.anchor_state.empty())
        doc["anchor_state"] = recipe.anchor_state;
    if (!recipe.patches.empty())
    {
        ojson ps = ojson::array();
        for (auto &p : recipe.patches)
        {
            static const char *digits = "0123456789ABCDEF";
            std::string hexs;
            for (auto b : p.bytes)
            {
                hexs += digits[b >> 4];
                hexs += digits[b & 15];
            }
            ps.push_back({ { "target", target_name(p.target) }, { "at", hex_addr(p.at) }, { "bytes", hexs } });
        }
        doc["patches"] = ps;
    }
    if (!recipe.pins.empty())
    {
        ojson ps = ojson::array();
        for (auto &pin : recipe.pins)
        {
            char value[8], was[8];
            snprintf(value, sizeof value, "0x%02X", pin.value);
            snprintf(was, sizeof was, "0x%02X", pin.was);
            ojson j = { { "rom", hex_addr(pin.offset) }, { "value", value }, { "was", was } };
            if (!pin.note.empty())
                j["note"] = pin.note;
            ps.push_back(j);
        }
        doc["pins"] = ps;
    }
    if (!recipe.switches.empty())
    {
        ojson ss = ojson::array();
        for (auto &s : recipe.switches)
        {
            char ret[8], was[8];
            snprintf(ret, sizeof ret, "0x%02X", s.ret);
            snprintf(was, sizeof was, "0x%02X", s.was);
            ojson j = { { "entry", hex_addr(s.entry) }, { "return", ret }, { "was", was } };
            if (s.hotkey)
                j["hotkey"] = s.hotkey;
            if (s.start_off)
                j["start_off"] = true;
            if (!s.note.empty())
                j["note"] = s.note;
            if (s.every && s.for_frames)
            {
                j["every"] = s.every;
                j["for"] = s.for_frames;
            }
            ss.push_back(j);
        }
        doc["switches"] = ss;
    }
    if (!recipe.changes.empty())
    {
        auto hexs = [](const std::vector<uint8_t> &bytes) {
            std::string out;
            char two[4];
            for (auto b : bytes)
            {
                snprintf(two, sizeof two, "%02X", b);
                out += two;
            }
            return out;
        };
        ojson cs = ojson::array();
        for (auto &c : recipe.changes)
        {
            ojson j = { { "function", hex_addr(c.function) },
                        { "at", hex_addr(c.at) },
                        { "kind", change_kind_name(c.kind) },
                        { "value", hexs(c.value) },
                        { "was", hexs(c.was) } };
            if (c.hotkey)
                j["hotkey"] = c.hotkey;
            if (!c.start_on)
                j["start_on"] = false;
            if (!c.note.empty())
                j["note"] = c.note;
            if (c.every && c.for_frames)
            {
                j["every"] = c.every;
                j["for"] = c.for_frames;
            }
            cs.push_back(j);
        }
        doc["changes"] = cs;
    }
    if (!recipe.protects.empty())
    {
        ojson ps = ojson::array();
        for (auto &p : recipe.protects)
        {
            ojson j = { { "entry", hex_addr(p.entry) } };
            if (p.deep)
                j["deep"] = true;
            ps.push_back(j);
        }
        doc["protect"] = ps;
    }
    if (recipe.crash_policy)
        doc["crash_policy"] = policy_name(*recipe.crash_policy);
    if (recipe.rom_policy)
        doc["rom_policy"] = rom_policy_name(*recipe.rom_policy);
    if (recipe.instant_revert)
        doc["instant_revert"] = *recipe.instant_revert;
    if (recipe.test_first)
        doc["test_first"] = *recipe.test_first;
    if (recipe.crash_detection)
        doc["crash_detection"] = detection_json(*recipe.crash_detection);

    ojson rules = ojson::array();
    for (auto &r : recipe.rules)
    {
        ojson j;
        j["name"] = r.name;
        j["target"] = target_name(r.range.target);
        if (r.rect)
            j["rect"] = { { "start", hex_addr(r.range.first) }, { "row", r.rect->row }, { "width", r.rect->width },
                          { "height", r.rect->height } };
        else if (r.region.empty())
            j["range"] = range_json(r.range);
        else
            j["range"] = r.region;
        j["op"] = op_json(r.op);
        j["when"] = when_json(r.when);
        j["count"] = r.count;
        if (r.pick == Pick::Sticky)
            j["pick"] = "sticky";
        if (!r.enabled)
            j["enabled"] = false;
        if (r.intensity != 1.0)
            j["intensity"] = r.intensity;
        if (r.avoid_hot)
            j["avoid_hot"] = true;
        if (r.aim.role != Role::Any)
            j["role"] = role_name(r.aim.role);
        if (r.aim.in_busy_functions)
            j["within"] = "busy_functions";
        for (auto [key, list] : { std::pair{ "classes", &r.aim.classes }, std::pair{ "except", &r.aim.except } })
            if (!list->empty())
            {
                auto names = json::array();
                for (auto c : *list)
                    names.push_back(class_name(c));
                j[key] = names;
            }
        if (r.salt)
            j["salt"] = r.salt;
        rules.push_back(j);
    }
    doc["rules"] = rules;
    return dump_compact(doc);
}

RegionsResult parse_regions(std::string_view json_text)
{
    RegionsResult result;
    Ctx ctx;

    json doc;
    try
    {
        doc = json::parse(json_text);
    }
    catch (const json::parse_error &e)
    {
        result.errors.push_back(std::string("not valid JSON: ") + e.what());
        return result;
    }

    if (!doc.is_object() || !doc.contains("regions") || !doc["regions"].is_object())
    {
        result.errors.push_back("regions: expected {\"format\": 1, \"regions\": {...}}");
        return result;
    }

    NominalMemory nominal;
    for (auto &[name, entry] : doc["regions"].items())
    {
        std::string path = "regions." + name;
        size_t before = ctx.errors.size();
        if (!require_object(ctx, entry, path))
            continue;
        check_keys(ctx, entry, path, { "target", "range", "notes" });
        if (!entry.contains("target") || !entry.contains("range"))
        {
            ctx.fail(path, "needs target and range");
            continue;
        }
        auto target = get_target(ctx, entry["target"], path + ".target");
        if (!target)
            continue;
        auto range = get_range(ctx, entry, path, target, {});
        if (!range || ctx.errors.size() != before)
            continue;
        if (auto err = validate_range(range->first, nominal))
        {
            ctx.fail(path + ".range", *err);
            continue;
        }
        result.regions[name] = range->first;
        if (entry.contains("notes") && entry["notes"].is_string())
            result.notes[name] = entry["notes"].get<std::string>();
    }
    result.errors = ctx.errors;
    return result;
}

std::string write_regions(const Regions &regions, const std::map<std::string, std::string> &notes)
{
    ojson doc;
    doc["format"] = 1;
    ojson all = ojson::object();
    for (auto &[name, r] : regions) // std::map: regions come out sorted by name
    {
        all[name] = { { "target", target_name(r.target) }, { "range", range_json(r) } };
        if (auto it = notes.find(name); it != notes.end() && !it->second.empty())
            all[name]["notes"] = it->second;
    }
    doc["regions"] = all;
    return dump_compact(doc);
}

std::optional<std::string> validate_region_name(std::string_view name)
{
    if (name.empty() || name.size() > 40)
        return "a region name needs 1 to 40 characters";
    auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    if (!letter(name[0]))
        return "a region name starts with a letter";
    for (char c : name)
        if (!letter(c) && !(c >= '0' && c <= '9') && c != ' ' && c != '_' && c != '-')
            return std::string("a region name can't contain '") + c + "'";
    return std::nullopt;
}

} // namespace corrupt
