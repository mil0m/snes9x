#include "script_engines.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cstdio>
#include <set>

using json = nlohmann::json;

namespace corrupt
{

namespace
{

std::string hex(uint32_t v, int digits)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%0*X", digits, v);
    return buf;
}

std::optional<uint32_t> parse_hex(const json &j)
{
    if (!j.is_string())
        return std::nullopt;
    std::string s = j.get<std::string>();
    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0)
        s = s.substr(2);
    if (s.empty() || s.size() > 8 || s.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return std::nullopt;
    return uint32_t(std::stoul(s, nullptr, 16));
}

// The bias a read implies for a rule of this stride: X wraps at 64 K, a ROM offset doesn't.
uint32_t bias_of(const ScriptObservation &o, int k, int stride)
{
    uint32_t b = o.index - uint32_t(stride) * o.value[k];
    return o.entry < 0 ? (b & 0xFFFF) : b;
}

} // namespace

void ScriptLog::note(uint32_t site, const ScriptObservation &o)
{
    auto &s = sites[site];
    s.dispatches++;
    s.through_pointer |= o.through_pointer;
    if (!o.has_index)
        return;
    // Each rule this dispatch fits, once, with the newest read that fits it.
    std::vector<std::pair<ScriptRuleKey, int>> fits;
    for (int k = 0; k < o.reads; k++)
    {
        if (k == o.entry)
            continue;
        for (int stride = 1; stride <= kMaxStride; stride++)
        {
            ScriptRuleKey key{ stride, bias_of(o, k, stride) };
            if (std::none_of(fits.begin(), fits.end(), [&](auto &f) { return f.first == key; }))
                fits.push_back({ key, k });
        }
    }
    for (auto &[key, k] : fits)
    {
        auto it = s.candidates.find(key);
        if (it == s.candidates.end())
        {
            make_room(s);
            it = s.candidates.emplace(key, ScriptCandidateStats{}).first;
        }
        auto &c = it->second;
        c.dispatches++;
        uint8_t v = o.value[k];
        uint32_t at = o.offset[k];
        c.handlers[v] = o.target;
        c.runs[v]++;
        if (c.last != 0xFFFFFFFF)
        {
            auto prev = c.places.find(c.last);
            if (prev != c.places.end())
            {
                // The same place again (a wait run each frame) says nothing about its length.
                int64_t step = int64_t(at) - int64_t(c.last);
                if (step >= 1 && step <= kMaxStep)
                    prev->second.steps[uint8_t(step)]++;
                else if (step != 0)
                    prev->second.wild++;
            }
        }
        auto [p, fresh] = c.places.try_emplace(at);
        if (fresh)
            places_seen++;
        p->second.value = v;
        p->second.runs++;
        c.last = at;
    }
}

// A full site keeps its busiest kCandidates (ties: the rule key, so it's deterministic).
void ScriptLog::make_room(ScriptSite &s)
{
    if (s.candidates.size() < kPrune)
        return;
    std::vector<std::pair<uint32_t, ScriptRuleKey>> ranked;
    for (auto &[key, c] : s.candidates)
        ranked.push_back({ c.dispatches, key });
    std::sort(ranked.begin(), ranked.end(), [](auto &a, auto &b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    for (size_t i = kCandidates; i < ranked.size(); i++)
        s.candidates.erase(ranked[i].second);
}

void ScriptLog::merge(const ScriptLog &other)
{
    for (auto &[site, theirs] : other.sites)
    {
        auto &mine = sites[site];
        mine.dispatches += theirs.dispatches;
        mine.through_pointer |= theirs.through_pointer;
        for (auto &[key, t] : theirs.candidates)
        {
            auto it = mine.candidates.find(key);
            if (it == mine.candidates.end())
            {
                make_room(mine);
                it = mine.candidates.emplace(key, ScriptCandidateStats{}).first;
            }
            auto &c = it->second;
            c.dispatches += t.dispatches;
            for (auto &[v, h] : t.handlers)
                c.handlers[v] = h;
            for (auto &[v, n] : t.runs)
                c.runs[v] += n;
            for (auto &[at, tp] : t.places)
            {
                auto [p, fresh] = c.places.try_emplace(at);
                if (fresh)
                    places_seen++;
                p->second.value = tp.value;
                p->second.runs += tp.runs;
                p->second.wild += tp.wild;
                for (auto &[step, n] : tp.steps)
                    p->second.steps[step] += n;
            }
        }
    }
}

std::vector<ScriptEnginePlace> ScriptEngine::usable_places() const
{
    std::vector<ScriptEnginePlace> out;
    for (auto &p : places)
    {
        auto c = commands.find(p.value);
        if (p.steady && c != commands.end() && c->second.usable())
            out.push_back(p);
    }
    return out;
}

std::vector<uint8_t> ScriptEngine::same_length(int length) const
{
    std::vector<uint8_t> out;
    for (auto &[v, c] : commands)
        if (c.usable() && c.length == length)
            out.push_back(v);
    return out;
}

namespace
{

ScriptEngine engine_of(uint32_t site, const ScriptSite &s, const ScriptRuleKey &key, const ScriptCandidateStats &c,
                       const ScriptRules &rules)
{
    ScriptEngine e;
    e.site = site;
    e.through_pointer = s.through_pointer;
    e.stride = key.first;
    e.bias = key.second;
    e.dispatches = c.dispatches;
    e.site_dispatches = s.dispatches;
    // Steps per command, over its places.
    std::map<uint8_t, std::map<uint8_t, uint32_t>> steps;
    std::map<uint8_t, uint32_t> wild;
    for (auto &[at, p] : c.places)
    {
        for (auto &[step, n] : p.steps)
            steps[p.value][step] += n;
        wild[p.value] += p.wild;
    }
    uint32_t all_samples = 0, all_wild = 0;
    for (auto &[v, n] : c.runs)
    {
        ScriptCommand cmd;
        cmd.value = v;
        cmd.runs = n;
        auto h = c.handlers.find(v);
        cmd.handler = h == c.handlers.end() ? 0 : h->second;
        uint32_t top = 0;
        for (auto &[step, k] : steps[v])
        {
            cmd.samples += k;
            if (k > top)
                top = k, cmd.length = step;
        }
        cmd.wild = wild[v];
        if (cmd.samples < rules.min_length_samples || top * 4 < cmd.samples * 3)
            cmd.length = 0;
        all_samples += cmd.samples;
        all_wild += cmd.wild;
        e.commands[v] = cmd;
    }
    // Interleaved scripts make every command's next dispatch land elsewhere some of the time;
    // a branch, a call or a jump does it much more often than the engine as a whole.
    double baseline = all_samples + all_wild ? double(all_wild) / double(all_samples + all_wild) : 0;
    e.sequential = all_samples + all_wild ? double(all_samples) / double(all_samples + all_wild) : 0;
    for (auto &[v, cmd] : e.commands)
    {
        uint32_t total = cmd.samples + cmd.wild;
        if (total >= 4 && double(cmd.wild) / double(total) > baseline + 0.25)
            cmd.branchy = true;
    }
    for (auto &[at, p] : c.places)
    {
        ScriptEnginePlace ep;
        ep.offset = at;
        ep.value = p.value;
        ep.runs = p.runs;
        int length = e.commands[p.value].length;
        // Trusted only where every step seen from here was the command's length.
        ep.steady = length > 0 && !p.steps.empty() &&
                    std::all_of(p.steps.begin(), p.steps.end(), [&](auto &st) { return st.first == length; });
        e.places.push_back(ep);
    }
    std::sort(e.places.begin(), e.places.end(), [](auto &a, auto &b) { return a.offset < b.offset; });
    if (!e.places.empty())
    {
        e.first = e.places.front().offset;
        e.last = e.places.back().offset;
    }
    return e;
}

bool big_enough(const ScriptEngine &e, const ScriptRules &rules)
{
    size_t commands = e.through_pointer ? std::max(rules.min_commands, rules.min_commands_through_ram) : rules.min_commands;
    return e.dispatches >= rules.min_dispatches && e.commands.size() >= commands &&
           e.places.size() >= rules.min_places && e.sequential >= rules.min_sequential &&
           double(e.places.size()) >= rules.min_places_per_command * double(e.commands.size());
}

} // namespace

std::vector<ScriptEngine> find_script_engines(const ScriptLog &log, const ScriptRules &rules)
{
    std::vector<ScriptEngine> out;
    for (auto &[site, s] : log.all())
    {
        // Busiest candidates first; ties by more places, then the lower stride and bias.
        std::vector<std::pair<ScriptRuleKey, const ScriptCandidateStats *>> ranked;
        for (auto &[key, c] : s.candidates)
            ranked.push_back({ key, &c });
        std::sort(ranked.begin(), ranked.end(), [](auto &a, auto &b) {
            if (a.second->dispatches != b.second->dispatches)
                return a.second->dispatches > b.second->dispatches;
            if (a.second->places.size() != b.second->places.size())
                return a.second->places.size() > b.second->places.size();
            return a.first < b.first;
        });
        if (ranked.empty())
            continue;
        if (!s.through_pointer)
        {
            // An indexed jump has one table: its best rule must explain nearly all of the site.
            auto e = engine_of(site, s, ranked[0].first, *ranked[0].second, rules);
            if (big_enough(e, rules) && e.dispatches >= rules.share * double(s.dispatches))
                out.push_back(std::move(e));
            continue;
        }
        // A jump through RAM can serve several dispatchers: each rule stands on its own counts,
        // but one that only echoes a bigger engine (mostly the same handlers) isn't another.
        std::vector<ScriptEngine> here;
        for (auto &[key, c] : ranked)
        {
            auto e = engine_of(site, s, key, *c, rules);
            if (!big_enough(e, rules))
                continue;
            bool echo = false;
            for (auto &big : here)
            {
                std::set<uint32_t> theirs;
                for (auto &[v, cmd] : big.commands)
                    theirs.insert(cmd.handler);
                size_t shared = 0;
                for (auto &[v, cmd] : e.commands)
                    shared += theirs.count(cmd.handler);
                if (shared * 5 >= e.commands.size() * 4)
                    echo = true;
            }
            if (!echo)
                here.push_back(std::move(e));
        }
        for (auto &e : here)
            out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) {
        return a.dispatches != b.dispatches ? a.dispatches > b.dispatches : a.site < b.site;
    });
    return out;
}

std::vector<uint8_t> script_marks(const std::vector<ScriptEngine> &engines, size_t rom_size)
{
    std::vector<uint8_t> marks(rom_size, 0);
    for (auto &e : engines)
        for (auto &p : e.places)
        {
            if (p.offset >= rom_size)
                continue;
            auto c = e.commands.find(p.value);
            bool usable = p.steady && c != e.commands.end() && c->second.usable();
            marks[p.offset] = std::max(marks[p.offset], usable ? scriptmark::Usable : scriptmark::Command);
            if (usable)
                for (int i = 1; i < c->second.length && p.offset + uint32_t(i) < rom_size; i++)
                    marks[p.offset + uint32_t(i)] = std::max(marks[p.offset + uint32_t(i)], scriptmark::Argument);
        }
    return marks;
}

ScriptAt script_at(const std::vector<ScriptEngine> &engines, uint32_t offset)
{
    ScriptAt out;
    for (auto &e : engines)
    {
        // The last place at or before the byte.
        auto it = std::upper_bound(e.places.begin(), e.places.end(), offset,
                                   [](uint32_t o, const ScriptEnginePlace &p) { return o < p.offset; });
        if (it == e.places.begin())
            continue;
        --it;
        if (it->offset == offset)
            return { &e, &*it, false };
        auto c = e.commands.find(it->value);
        if (it->steady && c != e.commands.end() && c->second.usable() && offset < it->offset + uint32_t(c->second.length))
            out = { &e, &*it, true };
    }
    return out;
}

void ScriptLogKeeper::reset(ScriptLogs loaded)
{
    own = std::move(loaded.mine);
    others = std::move(loaded.others);
    saved_print = own.fingerprint();
    frames = 0;
}

ScriptLog ScriptLogKeeper::combined() const
{
    ScriptLog all = own;
    all.merge(others);
    return all;
}

bool ScriptLogKeeper::save_due()
{
    if (frames < kSaveEveryFrames)
        return false;
    frames = 0;
    return unsaved();
}

std::string encode_script_log(const ScriptLog &log, size_t rom_size, uint32_t rom_crc, uint32_t keep,
                              size_t min_places, size_t per_site)
{
    char crc[16];
    std::snprintf(crc, sizeof crc, "%08X", rom_crc);
    std::string out = "{\"format\": \"snes9x-scripts\", \"version\": 1, \"rom_size\": " + std::to_string(rom_size) +
                      ", \"rom_crc\": \"" + crc + "\", \"sites\": [";
    bool first_site = true;
    for (auto &[site, s] : log.all())
    {
        std::vector<std::pair<ScriptRuleKey, const ScriptCandidateStats *>> chosen;
        for (auto &[key, c] : s.candidates)
            if (c.dispatches >= keep && c.places.size() >= min_places)
                chosen.push_back({ key, &c });
        std::sort(chosen.begin(), chosen.end(), [](auto &a, auto &b) {
            return a.second->dispatches != b.second->dispatches ? a.second->dispatches > b.second->dispatches
                                                                 : a.first < b.first;
        });
        if (chosen.size() > per_site)
            chosen.resize(per_site);
        std::sort(chosen.begin(), chosen.end(), [](auto &a, auto &b) { return a.first < b.first; });
        json rules = json::array();
        for (auto &[key, cp] : chosen)
        {
            auto &c = *cp;
            json commands = json::array();
            for (auto &[v, n] : c.runs)
            {
                auto h = c.handlers.find(v);
                char vb[4];
                std::snprintf(vb, sizeof vb, "%02X", v);
                commands.push_back({ vb, hex(h == c.handlers.end() ? 0 : h->second, 6), n });
            }
            std::vector<uint32_t> at;
            for (auto &[o, p] : c.places)
                at.push_back(o);
            std::sort(at.begin(), at.end());
            json places = json::array();
            for (uint32_t o : at)
            {
                auto &p = c.places.at(o);
                json steps = json::array();
                for (auto &[step, n] : p.steps)
                    steps.push_back({ step, n });
                char vb[4];
                std::snprintf(vb, sizeof vb, "%02X", p.value);
                places.push_back({ hex(o, 6), vb, p.runs, p.wild, steps });
            }
            rules.push_back({ { "stride", key.first },
                              { "bias", hex(key.second, 6) },
                              { "dispatches", c.dispatches },
                              { "commands", commands },
                              { "places", places } });
        }
        if (rules.empty())
            continue;
        json j = { { "site", hex(site, 6) },
                   { "through_pointer", s.through_pointer },
                   { "dispatches", s.dispatches },
                   { "rules", rules } };
        out += (first_site ? "\n  " : ",\n  ") + j.dump();
        first_site = false;
    }
    out += first_site ? "]}\n" : "\n]}\n";
    return out;
}

std::optional<ScriptLog> decode_script_log(std::string_view file, size_t rom_size, uint32_t rom_crc, std::string &error)
{
    json j = json::parse(file, nullptr, false);
    if (j.is_discarded() || !j.is_object() || j.value("format", std::string()) != "snes9x-scripts")
    {
        error = "not a script engine file";
        return std::nullopt;
    }
    if (!j.contains("rom_size") || !j["rom_size"].is_number_unsigned() || j["rom_size"].get<uint64_t>() != rom_size)
    {
        error = "recorded for another ROM size";
        return std::nullopt;
    }
    if (!j.contains("rom_crc") || parse_hex(j["rom_crc"]) != rom_crc)
    {
        error = "recorded for another version of the game";
        return std::nullopt;
    }
    if (!j.contains("sites") || !j["sites"].is_array())
    {
        error = "no sites in the file";
        return std::nullopt;
    }
    auto fail = [&](const char *what) -> std::optional<ScriptLog> {
        error = std::string("a site with a missing or bad ") + what;
        return std::nullopt;
    };
    auto count = [](const json &v) {
        return v.is_number_unsigned() ? std::optional<uint32_t>(uint32_t(v.get<uint64_t>())) : std::nullopt;
    };
    ScriptLog log;
    for (auto &js : j["sites"])
    {
        if (!js.is_object())
            return fail("entry");
        auto site = js.contains("site") ? parse_hex(js["site"]) : std::nullopt;
        auto dispatches = js.contains("dispatches") ? count(js["dispatches"]) : std::nullopt;
        if (!site || *site >= rom_size || !dispatches || !js.contains("rules") || !js["rules"].is_array())
            return fail("site, dispatches or rules");
        ScriptSite s;
        s.dispatches = *dispatches;
        s.through_pointer = js.value("through_pointer", false);
        for (auto &jr : js["rules"])
        {
            if (!jr.is_object() || !jr.contains("stride") || !jr["stride"].is_number_unsigned())
                return fail("rule");
            int stride = int(jr["stride"].get<uint64_t>());
            auto bias = jr.contains("bias") ? parse_hex(jr["bias"]) : std::nullopt;
            auto rd = jr.contains("dispatches") ? count(jr["dispatches"]) : std::nullopt;
            if (stride < 1 || stride > kMaxStride || !bias || !rd || !jr.contains("commands") ||
                !jr["commands"].is_array() || !jr.contains("places") || !jr["places"].is_array())
                return fail("rule");
            ScriptCandidateStats c;
            c.dispatches = *rd;
            for (auto &jc : jr["commands"])
            {
                if (!jc.is_array() || jc.size() != 3)
                    return fail("command");
                auto v = parse_hex(jc[0]);
                auto h = parse_hex(jc[1]);
                auto n = count(jc[2]);
                if (!v || *v > 0xFF || !h || *h > 0xFFFFFF || !n)
                    return fail("command");
                c.handlers[uint8_t(*v)] = *h;
                c.runs[uint8_t(*v)] = *n;
            }
            for (auto &jp : jr["places"])
            {
                if (!jp.is_array() || jp.size() != 5 || !jp[4].is_array())
                    return fail("place");
                auto o = parse_hex(jp[0]);
                auto v = parse_hex(jp[1]);
                auto runs = count(jp[2]);
                auto wild = count(jp[3]);
                if (!o || *o >= rom_size || !v || *v > 0xFF || !runs || !wild)
                    return fail("place");
                ScriptPlace p;
                p.value = uint8_t(*v);
                p.runs = *runs;
                p.wild = *wild;
                for (auto &st : jp[4])
                {
                    if (!st.is_array() || st.size() != 2 || !st[0].is_number_unsigned())
                        return fail("step");
                    auto n = count(st[1]);
                    uint64_t step = st[0].get<uint64_t>();
                    if (!n || step < 1 || step > uint64_t(kMaxStep))
                        return fail("step");
                    p.steps[uint8_t(step)] = *n;
                }
                c.places[*o] = std::move(p);
            }
            s.candidates[{ stride, *bias }] = std::move(c);
        }
        log.put(*site, std::move(s));
    }
    error.clear();
    return log;
}

} // namespace corrupt
