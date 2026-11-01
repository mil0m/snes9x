#include "calls.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <tuple>

namespace corrupt
{

using nlohmann::json;

std::vector<uint32_t> CallLog::end_frame()
{
    std::sort(fresh.begin(), fresh.end());
    fresh.erase(std::unique(fresh.begin(), fresh.end()), fresh.end());
    for (uint32_t entry : fresh)
        frames[entry]++;
    std::vector<uint32_t> called;
    called.swap(fresh);
    now++;
    return called;
}

void CallLog::add(const CallKey &key, uint8_t opcode, uint32_t hits)
{
    auto &seen = calls[key];
    if (!seen.hits)
        seen.opcode = opcode;
    seen.hits += hits;
}

void CallLog::merge(const CallLog &other)
{
    for (auto &[key, seen] : other.calls)
        add(key, seen.opcode, seen.hits);
    for (auto [entry, n] : other.frames)
        frames[entry] += n;
}

void CallLog::clear()
{
    calls.clear();
    frames.clear();
    fresh.clear();
}

namespace
{

std::string hex(uint32_t v, int digits)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%0*X", digits, v);
    return buf;
}

// "0x1234" (or "1234") as a number; nothing for anything else.
std::optional<uint32_t> parse_hex(const json &j)
{
    if (!j.is_string())
        return std::nullopt;
    std::string s = j.get<std::string>();
    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0)
        s = s.substr(2);
    if (s.empty() || s.size() > 8)
        return std::nullopt;
    uint32_t v = 0;
    for (char c : s)
    {
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0)
            return std::nullopt;
        v = v * 16 + uint32_t(d);
    }
    return v;
}

std::optional<uint64_t> field_count(const json &e, const char *name)
{
    if (!e.is_object() || !e.contains(name) || !e[name].is_number_unsigned())
        return std::nullopt;
    return e[name].get<uint64_t>();
}

std::optional<uint32_t> field_hex(const json &e, const char *name)
{
    return e.is_object() && e.contains(name) ? parse_hex(e[name]) : std::nullopt;
}

} // namespace

std::string encode_call_log(const CallLog &log, size_t rom_size)
{
    std::vector<std::pair<CallKey, CallSeen>> calls(log.all().begin(), log.all().end());
    std::sort(calls.begin(), calls.end(), [](auto &a, auto &b) {
        return std::tie(a.first.site, a.first.target) < std::tie(b.first.site, b.first.target);
    });
    std::map<uint32_t, uint64_t> frames(log.frames_run().begin(), log.frames_run().end());
    // One entry a line: readable, and diffs well in the shared library.
    std::string out = "{\"format\": \"snes9x-calls\", \"version\": 1, \"rom_size\": " + std::to_string(rom_size) +
                      ", \"calls\": [";
    for (size_t i = 0; i < calls.size(); i++)
    {
        char op[4];
        std::snprintf(op, sizeof op, "%02X", calls[i].second.opcode);
        json c = { { "site", hex(calls[i].first.site, 6) },
                   { "op", op },
                   { "target", hex(calls[i].first.target, 6) },
                   { "hits", calls[i].second.hits } };
        out += (i ? ",\n  " : "\n  ") + c.dump();
    }
    out += calls.empty() ? "], \"functions\": [" : "\n], \"functions\": [";
    size_t i = 0;
    for (auto [entry, n] : frames)
    {
        json f = { { "entry", hex(entry, 6) }, { "frames", n } };
        out += (i++ ? ",\n  " : "\n  ") + f.dump();
    }
    out += frames.empty() ? "]}\n" : "\n]}\n";
    return out;
}

std::optional<CallLog> decode_call_log(std::string_view file, size_t rom_size, std::string &error)
{
    json j = json::parse(file, nullptr, false);
    if (j.is_discarded() || !j.is_object() || j.value("format", std::string()) != "snes9x-calls")
    {
        error = "not a call file";
        return std::nullopt;
    }
    if (!j.contains("rom_size") || !j["rom_size"].is_number_unsigned() || j["rom_size"].get<uint64_t>() != rom_size)
    {
        error = "recorded for another ROM size";
        return std::nullopt;
    }
    if (!j.contains("calls") || !j["calls"].is_array())
    {
        error = "no calls in the file";
        return std::nullopt;
    }
    CallLog log;
    // A call or function that doesn't fit this ROM is dropped: the rest is still good.
    for (auto &e : j["calls"])
    {
        auto site = field_hex(e, "site"), target = field_hex(e, "target"), op = field_hex(e, "op");
        auto hits = field_count(e, "hits");
        if (!site || !target || !op || !hits || *site >= rom_size || *target >= rom_size || *op > 0xFF ||
            !callop::noted(uint8_t(*op)))
            continue;
        log.add(CallKey{ *site, *target }, uint8_t(*op), uint32_t(std::min<uint64_t>(*hits, UINT32_MAX)));
    }
    if (j.contains("functions") && j["functions"].is_array())
        for (auto &e : j["functions"])
        {
            auto entry = field_hex(e, "entry");
            auto n = field_count(e, "frames");
            if (entry && n && *entry < rom_size)
                log.add_frames(*entry, *n);
        }
    error.clear();
    return log;
}

void CallKeeper::reset(CallLogs loaded)
{
    own = std::move(loaded.mine);
    others = std::move(loaded.others);
    saved_print = own.fingerprint();
    frames = 0;
    forget_now();
}

void CallKeeper::end_frame()
{
    frames++;
    auto called = own.end_frame();
    for (uint32_t entry : called)
        now[entry]++;
    recent.push_back(std::move(called));
    if (recent.size() > kNowFrames)
    {
        for (uint32_t entry : recent.front())
            if (auto it = now.find(entry); it != now.end() && !--it->second)
                now.erase(it);
        recent.pop_front();
    }
}

void CallKeeper::forget_now()
{
    recent.clear();
    now.clear();
}

CallLog CallKeeper::combined() const
{
    CallLog all = own;
    all.merge(others);
    return all;
}

std::vector<Function> CallKeeper::functions() const
{
    std::unordered_map<uint32_t, Function> by_entry;
    auto add = [&](const CallLog &log) {
        for (auto &[key, seen] : log.all())
        {
            auto [it, fresh] = by_entry.try_emplace(key.target);
            Function &f = it->second;
            CallType t = call_type_of(seen.opcode);
            if (fresh)
            {
                f.entry = key.target;
                f.type = t;
                f.first_site = key.site;
            }
            else
            {
                if (f.type != t)
                    f.type = CallType::Mixed;
                f.first_site = std::min(f.first_site, key.site);
            }
            f.calls += seen.hits;
        }
        for (auto [entry, n] : log.frames_run())
            if (auto it = by_entry.find(entry); it != by_entry.end())
                it->second.frames += n;
    };
    add(own);
    add(others);
    // Callers: distinct sites across both logs.
    std::unordered_map<uint32_t, std::vector<uint32_t>> sites;
    for (const CallLog *log : { &own, &others })
        for (auto &[key, seen] : log->all())
            sites[key.target].push_back(key.site);
    std::vector<Function> out;
    out.reserve(by_entry.size());
    for (auto &[entry, f] : by_entry)
    {
        auto &s = sites[entry];
        std::sort(s.begin(), s.end());
        f.callers = uint32_t(std::unique(s.begin(), s.end()) - s.begin());
        if (auto it = now.find(entry); it != now.end())
            f.now = it->second;
        out.push_back(f);
    }
    std::sort(out.begin(), out.end(), [](const Function &a, const Function &b) {
        return std::tie(b.now, b.frames, a.entry) < std::tie(a.now, a.frames, b.entry);
    });
    return out;
}

bool CallKeeper::save_due()
{
    if (frames < kSaveEveryFrames)
        return false;
    frames = 0;
    return unsaved();
}

std::vector<Function> busiest(std::vector<Function> functions, size_t limit)
{
    std::erase_if(functions, [](const Function &f) { return f.type == CallType::Mixed; });
    std::sort(functions.begin(), functions.end(), [](const Function &a, const Function &b) {
        return std::tie(b.frames, a.entry) < std::tie(a.frames, b.entry);
    });
    if (limit && functions.size() > limit)
        functions.resize(limit);
    std::sort(functions.begin(), functions.end(), [](const Function &a, const Function &b) { return a.entry < b.entry; });
    return functions;
}

} // namespace corrupt
