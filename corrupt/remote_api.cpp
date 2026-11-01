#include "remote_api.h"
#include "byte_colour.h"
#include "nlohmann/json.hpp"

#include <cstdio>
#include <random>

namespace corrupt::remote
{

using json = nlohmann::json;

namespace
{

std::string error_reply(const std::string &why, const json &id)
{
    return json{ { "error", why }, { "id", id } }.dump();
}

std::optional<size_t> index_of(Target t)
{
    for (size_t i = 0; i < kAllTargets.size(); i++)
        if (kAllTargets[i] == t)
            return i;
    return std::nullopt;
}

// A number in [0, 1], or nothing.
std::optional<double> unit(const json &j, const char *key)
{
    if (!j.contains(key) || !j[key].is_number())
        return std::nullopt;
    double v = j[key].get<double>();
    if (!(v >= 0.0 && v <= 1.0))
        return std::nullopt;
    return v;
}

std::optional<int64_t> integer(const json &j, const char *key)
{
    if (!j.contains(key) || !j[key].is_number_integer())
        return std::nullopt;
    return j[key].get<int64_t>();
}

std::optional<bool> flag(const json &j, const char *key)
{
    if (!j.contains(key) || !j[key].is_boolean())
        return std::nullopt;
    return j[key].get<bool>();
}

} // namespace

bool Gate::open(Clock::time_point now)
{
    if (shut_until && now < *shut_until)
        return false;
    shut_until.reset();
    return true;
}

void Gate::failed(Clock::time_point now)
{
    failures.push_back(now);
    while (!failures.empty() && now - failures.front() > std::chrono::minutes(1))
        failures.pop_front();
    if (failures.size() >= kTries)
    {
        shut_until = now + std::chrono::minutes(1);
        failures.clear();
    }
}

Connection::Reply Connection::handle(std::string_view message, Clock::time_point now)
{
    Reply out;
    json j = json::parse(message, nullptr, false);
    if (j.is_discarded() || !j.is_object())
    {
        out.text = error_reply("not a message", nullptr);
        out.close = true;
        return out;
    }
    json id = j.contains("id") && j["id"].is_number_integer() ? j["id"] : json(nullptr);

    if (!ok)
    {
        // The handshake: nothing else before it.
        out.close = true;
        if (!j.contains("hello"))
        {
            out.text = error_reply("say hello first", id);
            return out;
        }
        if (!gate.open(now))
        {
            out.text = error_reply("too many wrong tokens; try again later", id);
            return out;
        }
        std::string given = j.contains("token") && j["token"].is_string() ? j["token"].get<std::string>() : "";
        if (!same_token(given, token))
        {
            gate.failed(now);
            out.text = error_reply("wrong token", id);
            return out;
        }
        ok = true;
        out.close = false;
        out.welcomed = true;
        out.text = json{ { "welcome", 1 }, { "colours", colour_table() } }.dump();
        out.did = "connected";
        return out;
    }

    if (j.contains("hello"))
    {
        out.text = error_reply("already connected", id);
        return out;
    }
    if (!j.contains("do") || !j["do"].is_string())
    {
        out.text = error_reply("no action", id);
        return out;
    }
    std::string action = j["do"].get<std::string>();
    auto done = [&](std::string did) {
        out.text = json{ { "done", id } }.dump();
        out.did = std::move(did);
        return out;
    };
    auto refuse = [&](const std::string &why) {
        out.text = error_reply(why, id);
        return out;
    };
    auto memory = [&]() -> std::optional<Target> {
        if (!j.contains("memory") || !j["memory"].is_string())
            return std::nullopt;
        return parse_target(j["memory"].get<std::string>());
    };

    if (action == "active")
    {
        auto on = flag(j, "on");
        if (!on)
            return refuse("active needs on: true or false");
        if (auto err = control.set_active(*on); !err.empty())
            return refuse(err);
        return done(*on ? "turned corruption on" : "turned corruption off");
    }
    if (action == "back_to_normal")
    {
        control.back_to_normal();
        return done("went back to normal");
    }
    if (action == "reroll")
    {
        control.reroll();
        return done("re-rolled");
    }
    if (action == "mark_crash")
    {
        control.mark_crash();
        return done("said it crashed");
    }
    if (action == "chaos")
    {
        auto v = unit(j, "value");
        if (!v)
            return refuse("chaos needs a value from 0 to 1");
        control.set_chaos(*v);
        return done("set Chaos to " + std::to_string(int(*v * 100 + 0.5)) + "%");
    }
    if (action == "rule_enabled" || action == "rule_strength")
    {
        auto rule = integer(j, "rule");
        if (!rule || *rule < 0 || size_t(*rule) >= control.rule_count())
            return refuse("no such rule");
        if (action == "rule_enabled")
        {
            auto on = flag(j, "on");
            if (!on)
                return refuse("rule_enabled needs on: true or false");
            control.set_rule_enabled(size_t(*rule), *on);
            return done(std::string(*on ? "turned on" : "turned off") + " rule " + std::to_string(*rule + 1));
        }
        auto v = unit(j, "value");
        if (!v)
            return refuse("rule_strength needs a value from 0 to 1");
        control.set_rule_strength(size_t(*rule), *v);
        return done("set rule " + std::to_string(*rule + 1) + " to " + std::to_string(int(*v * 100 + 0.5)) + "%");
    }
    if (action == "starter")
    {
        auto index = integer(j, "index");
        if (!index || *index < 0 || size_t(*index) >= control.starter_count())
            return refuse("no such starter");
        if (auto err = control.load_starter(size_t(*index)); !err.empty())
            return refuse(err);
        return done("loaded the starter " + control.starter_name(size_t(*index)));
    }
    if (action == "hotkey")
    {
        auto key = integer(j, "key");
        if (!key || *key < 1 || *key > kMaxHotkey)
            return refuse("hotkeys are 1 to 9");
        control.press_hotkey(int(*key));
        return done("pressed hotkey " + std::to_string(*key));
    }
    if (action == "watch")
    {
        if (j.contains("memory") && j["memory"] == "none")
        {
            out.watch_changed = watching.has_value();
            watching.reset();
            return done("");
        }
        auto t = memory();
        auto offset = integer(j, "offset");
        auto length = integer(j, "length");
        if (!t)
            return refuse("no such memory");
        size_t size = control.memory_size(*t);
        if (!offset || !length || *offset < 0 || *length <= 0 || *length > int64_t(kMaxWatch) ||
            uint64_t(*offset) + uint64_t(*length) > size)
            return refuse("the window must be inside the memory and at most 128 KB");
        Watch w{ *t, uint32_t(*offset), uint32_t(*length) };
        out.watch_changed = watching != w;
        watching = w;
        return done("");
    }
    if (action == "paint")
    {
        auto t = memory();
        if (!t)
            return refuse("no such memory");
        auto value = integer(j, "value");
        if (!value || *value < 0 || *value > 255)
            return refuse("the value must be 0 to 255");
        if (!j.contains("offsets") || !j["offsets"].is_array() || j["offsets"].empty())
            return refuse("paint needs offsets");
        if (j["offsets"].size() > kMaxPaint)
            return refuse("at most 4096 bytes at once");
        size_t size = control.memory_size(*t);
        std::vector<uint32_t> offsets;
        for (auto &o : j["offsets"])
        {
            if (!o.is_number_integer() || o.get<int64_t>() < 0 || uint64_t(o.get<int64_t>()) >= size)
                return refuse("an offset outside the memory");
            offsets.push_back(uint32_t(o.get<int64_t>()));
        }
        while (!paints.empty() && now - paints.front() >= std::chrono::seconds(1))
            paints.pop_front();
        if (paints.size() >= kPaintsPerSecond)
            return refuse("painting too fast");
        paints.push_back(now);
        if (auto err = control.paint(*t, offsets, uint8_t(*value)); !err.empty())
            return refuse(err);
        char what[96];
        snprintf(what, sizeof what, "painted %zu %s of %s with 0x%02X", offsets.size(),
                 offsets.size() == 1 ? "byte" : "bytes", target_name(*t), unsigned(*value));
        return done(what);
    }
    return refuse("unknown action");
}

std::string status_json(const StatusView &s)
{
    json rules = json::array();
    for (auto &r : s.rules)
        rules.push_back({ { "name", r.name },
                          { "what", r.what },
                          { "enabled", r.enabled },
                          { "strength", r.strength },
                          { "hotkey", r.hotkey } });
    json sizes = json::object();
    for (auto t : kAllTargets)
        sizes[target_name(t)] = s.sizes[size_t(t)];
    return json{ { "status",
                   { { "game_loaded", s.game_loaded },
                     { "game", s.game },
                     { "recipe", s.recipe },
                     { "active", s.active },
                     { "starting", s.starting },
                     { "has_recipe", s.has_recipe },
                     { "chaos", s.chaos },
                     { "seed", std::to_string(s.seed) },
                     { "crashes", s.crashes },
                     { "message", s.message },
                     { "rules", rules },
                     { "sizes", sizes },
                     { "clients", s.clients },
                     { "starters", s.starters } } } }
        .dump();
}

bool same_token(std::string_view a, std::string_view b)
{
    if (a.empty() || b.empty())
        return false;
    // Walks the whole of `b` (the real token) whatever `a` is.
    unsigned diff = unsigned(a.size() ^ b.size());
    for (size_t i = 0; i < b.size(); i++)
        diff |= unsigned(uint8_t(i < a.size() ? a[i] : 0) ^ uint8_t(b[i]));
    return diff == 0;
}

std::string new_token()
{
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::random_device rd; // the system's source (arc4random on macOS, getrandom on Linux)
    std::array<uint8_t, 32> bytes;
    for (size_t i = 0; i < bytes.size(); i += 4)
    {
        uint32_t r = rd();
        for (size_t k = 0; k < 4; k++)
            bytes[i + k] = uint8_t(r >> (8 * k));
    }
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    for (uint8_t b : bytes)
    {
        acc = (acc << 8) | b;
        bits += 8;
        while (bits >= 6)
        {
            bits -= 6;
            out += kAlphabet[(acc >> bits) & 63];
        }
    }
    if (bits)
        out += kAlphabet[(acc << (6 - bits)) & 63];
    return out;
}

std::vector<std::string> colour_table()
{
    std::vector<std::string> out;
    for (int v = 0; v < 256; v++)
    {
        auto c = byte_colour(uint8_t(v));
        char buf[8];
        snprintf(buf, sizeof buf, "#%02x%02x%02x", c.r, c.g, c.b);
        out.push_back(buf);
    }
    return out;
}

std::vector<uint8_t> memory_message(Target t, uint32_t offset, std::span<const uint8_t> bytes)
{
    std::vector<uint8_t> m = { 'S', '9', 'X', 'M', uint8_t(index_of(t).value_or(0xFF)) };
    for (int k = 0; k < 4; k++)
        m.push_back(uint8_t(offset >> (8 * k)));
    m.insert(m.end(), bytes.begin(), bytes.end());
    return m;
}

} // namespace corrupt::remote
