#include "recipe_json.h"

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
    size_t size(Target t) const override { return t == Target::SRAM ? 0x20000 : nominal_size(t); }
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
    if (s.empty())
        return std::nullopt;
    size_t i = 0;
    bool negative = false;
    if (s[0] == '-')
    {
        negative = true;
        i = 1;
    }
    int base = 10;
    if (s.size() > i + 1 && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X'))
    {
        base = 16;
        i += 2;
    }
    if (i >= s.size())
        return std::nullopt;

    uint64_t value = 0;
    for (; i < s.size(); i++)
    {
        int digit;
        char c = s[i];
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else
            return std::nullopt;
        value = value * uint64_t(base) + uint64_t(digit);
    }
    return negative ? -int64_t(value) : int64_t(value);
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
        if (*target == Target::SRAM)
        {
            ctx.fail(rpath, "sram rules need an explicit range (its size depends on the game)");
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
    check_keys(ctx, o, path, { "type", "min", "max", "mode", "bits", "mask", "width", "a", "b", "from", "length", "value", "window" });

    if (!o.contains("type") || !o["type"].is_string() || !parse_op(o["type"].get<std::string>()))
    {
        ctx.fail(path + ".type", "expected one of add, random, bitflip, swap, copy, freeze, shuffle");
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

    if (o.contains("mode"))
    {
        auto mode = o["mode"].is_string() ? o["mode"].get<std::string>() : "";
        if (mode == "clamp")
            op.clamp = true;
        else if (mode != "wrap")
            ctx.fail(path + ".mode", "expected \"wrap\" or \"clamp\"");
    }

    if (o.contains("from"))
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
    check_keys(ctx, w, path, { "once_at_frame", "every_frames", "chance_per_frame", "hotkey", "window" });

    int kinds = 0;
    for (auto key : { "once_at_frame", "every_frames", "chance_per_frame", "hotkey" })
        kinds += w.contains(key);
    if (kinds != 1)
    {
        ctx.fail(path, "needs exactly one of once_at_frame, every_frames, chance_per_frame, hotkey");
        return;
    }

    if (w.contains("once_at_frame"))
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
    check_keys(ctx, r, path, { "name", "target", "range", "op", "when", "count", "pick", "enabled", "intensity", "avoid_hot", "salt" });

    Rule rule;
    rule.name = r.contains("name") && r["name"].is_string() ? r["name"].get<std::string>() : "rule " + std::to_string(index + 1);

    std::optional<Target> target;
    if (r.contains("target"))
    {
        target = get_target(ctx, r["target"], path + ".target");
        if (!target)
            return std::nullopt;
    }
    auto range = get_range(ctx, r, path, target, regions);
    if (range)
    {
        rule.range = range->first;
        rule.region = range->second;
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
    check_keys(ctx, d, path, { "enabled", "cpu_stopped", "runaway_frames", "nmi_off_seconds", "frozen_seconds", "black_seconds" });

    for (auto [key, out] : { std::pair{ "enabled", &cfg.enabled }, std::pair{ "cpu_stopped", &cfg.cpu_stopped } })
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
    return d;
}

} // namespace

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
    check_keys(ctx, doc, "recipe", { "format", "name", "notes", "game", "seed", "anchor_state", "rules", "crash_policy", "crash_detection" });

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

    if (doc.contains("crash_policy"))
    {
        const json &p = doc["crash_policy"];
        if (p.is_string() && parse_policy(p.get<std::string>()))
            recipe.crash_policy = parse_policy(p.get<std::string>());
        else
            ctx.fail("recipe.crash_policy", "expected one of reroll, skip, dampen, disable, escalate");
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
    if (!recipe.anchor_state.empty())
        doc["anchor_state"] = recipe.anchor_state;
    if (recipe.crash_policy)
        doc["crash_policy"] = policy_name(*recipe.crash_policy);
    if (recipe.crash_detection)
        doc["crash_detection"] = detection_json(*recipe.crash_detection);

    ojson rules = ojson::array();
    for (auto &r : recipe.rules)
    {
        ojson j;
        j["name"] = r.name;
        j["target"] = target_name(r.range.target);
        if (r.region.empty())
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
    }
    result.errors = ctx.errors;
    return result;
}

std::string write_regions(const Regions &regions)
{
    ojson doc;
    doc["format"] = 1;
    ojson all = ojson::object();
    for (auto &[name, r] : regions) // std::map: regions come out sorted by name
        all[name] = { { "target", target_name(r.target) }, { "range", range_json(r) } };
    doc["regions"] = all;
    return dump_compact(doc);
}

} // namespace corrupt
