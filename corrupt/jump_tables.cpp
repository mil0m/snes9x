#include "jump_tables.h"
#include "code_map.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <tuple>

namespace corrupt
{

namespace cf = codeflag;
using nlohmann::json;

void JumpLog::add(const JumpKey &key, JumpSeen seen)
{
    auto &mine = jumps[key];
    if (!mine.hits)
        mine.opcode = seen.opcode;
    mine.hits += seen.hits;
}

void JumpLog::merge(const JumpLog &other)
{
    for (auto &[key, seen] : other.jumps)
        add(key, seen);
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

} // namespace

std::string encode_jump_log(const JumpLog &log, size_t rom_size)
{
    std::vector<std::pair<JumpKey, JumpSeen>> sorted(log.all().begin(), log.all().end());
    std::sort(sorted.begin(), sorted.end(), [](auto &a, auto &b) {
        return std::tie(a.first.site, a.first.entry, a.first.target) < std::tie(b.first.site, b.first.entry, b.first.target);
    });
    json jumps = json::array();
    for (auto &[key, seen] : sorted)
    {
        char op[4];
        std::snprintf(op, sizeof op, "%02X", seen.opcode);
        jumps.push_back({ { "site", hex(key.site, 6) },
                          { "op", op },
                          { "entry", key.entry == kNoEntry ? json(nullptr) : json(hex(key.entry, 6)) },
                          { "target", hex(key.target, 6) },
                          { "hits", seen.hits } });
    }
    // One jump a line: readable, and diffs well in the shared library.
    std::string out = "{\"format\": \"snes9x-jumps\", \"version\": 1, \"rom_size\": " + std::to_string(rom_size) +
                      ", \"jumps\": [";
    for (size_t i = 0; i < jumps.size(); i++)
        out += (i ? ",\n  " : "\n  ") + jumps[i].dump();
    out += jumps.empty() ? "]}\n" : "\n]}\n";
    return out;
}

std::optional<JumpLog> decode_jump_log(std::string_view file, size_t rom_size, std::string &error)
{
    json j = json::parse(file, nullptr, false);
    if (j.is_discarded() || !j.is_object())
    {
        error = "not a jump table file";
        return std::nullopt;
    }
    if (j.value("format", std::string()) != "snes9x-jumps")
    {
        error = "not a jump table file";
        return std::nullopt;
    }
    if (!j.contains("rom_size") || !j["rom_size"].is_number_unsigned() || j["rom_size"].get<uint64_t>() != rom_size)
    {
        error = "recorded for another ROM size";
        return std::nullopt;
    }
    JumpLog log;
    if (!j.contains("jumps") || !j["jumps"].is_array())
    {
        error = "no jumps in the file";
        return std::nullopt;
    }
    for (auto &e : j["jumps"])
    {
        auto site = e.is_object() && e.contains("site") ? parse_hex(e["site"]) : std::nullopt;
        auto target = e.is_object() && e.contains("target") ? parse_hex(e["target"]) : std::nullopt;
        auto op = e.is_object() && e.contains("op") ? parse_hex(e["op"]) : std::nullopt;
        std::optional<uint32_t> entry = kNoEntry;
        if (e.is_object() && e.contains("entry") && !e["entry"].is_null())
            entry = parse_hex(e["entry"]);
        bool hits_ok = e.is_object() && e.contains("hits") && e["hits"].is_number_unsigned();
        if (!site || !target || !op || !entry || !hits_ok || *op > 0xFF || *site >= rom_size ||
            (*entry != kNoEntry && *entry >= rom_size) || *target > 0xFFFFFF)
        {
            error = "a jump with a missing or bad field";
            return std::nullopt;
        }
        log.add(JumpKey{ *site, *entry, *target }, JumpSeen{ uint8_t(*op), uint32_t(e["hits"].get<uint64_t>()) });
    }
    error.clear();
    return log;
}

size_t JumpTable::seen() const
{
    return size_t(std::count_if(entries.begin(), entries.end(), [](auto &e) { return e.seen; }));
}

std::vector<uint8_t> JumpTable::bytes_for(uint32_t target) const
{
    std::vector<uint8_t> out = { uint8_t(target), uint8_t(target >> 8) };
    if (stride == 3)
        out.push_back(uint8_t(target >> 16));
    return out;
}

namespace
{

struct Found
{
    uint32_t entry;
    uint32_t target;
    uint32_t hits;
    uint8_t opcode;
    JumpKind kind;
    int stride;
};

class Finder
{
  public:
    Finder(std::span<const uint8_t> rom, std::span<const uint8_t> flags, const RomOffsets &offsets)
        : rom(rom), flags(flags), offsets(offsets)
    {
    }

    uint8_t flag(size_t o) const { return o < flags.size() ? uint8_t(flags[o] & cf::All) : 0; }
    bool data(size_t o) const { return o < rom.size() && (flag(o) & cf::Data); }
    uint16_t word(size_t o) const { return uint16_t(rom[o] | (rom[o + 1] << 8)); }

    // Every spot holding a 16-bit value the CPU read as data, by value. Built on first use.
    const std::vector<uint32_t> &spots(uint16_t value)
    {
        if (!indexed)
        {
            for (size_t o = 0; o + 1 < rom.size(); o++)
                if (data(o) && data(o + 1))
                    by_value[word(o)].push_back(uint32_t(o));
            indexed = true;
        }
        static const std::vector<uint32_t> none;
        auto it = by_value.find(value);
        return it == by_value.end() ? none : it->second;
    }

    // The value at `o` could be one more routine of a table whose targets are in `bank`.
    bool plausible(size_t o, int stride, uint32_t bank) const
    {
        if (o + size_t(stride) > rom.size())
            return false;
        for (int i = 0; i < stride; i++)
            if (flag(o + size_t(i)) & cf::Code)
                return false;
        uint32_t target_bank = stride == 3 ? rom[o + 2] : bank;
        if (target_bank != bank)
            return false;
        // The routine must have run (from somewhere): in HiROM nearly every 16-bit value
        // lands in ROM, so anything looser walks off through plain data.
        auto at = offsets.offset((target_bank << 16) | word(o));
        return at && *at < rom.size() && (flag(*at) & cf::Opcode);
    }

    uint32_t target_at(size_t o, int stride, uint32_t bank) const
    {
        uint32_t b = stride == 3 ? rom[o + 2] : bank;
        return (b << 16) | word(o);
    }

    std::span<const uint8_t> rom, flags;
    const RomOffsets &offsets;

  private:
    bool indexed = false;
    std::unordered_map<uint16_t, std::vector<uint32_t>> by_value;
};

// One cluster of seen entries, filled out with likely ones.
JumpTable make_table(const Finder &f, uint32_t site, const std::vector<Found> &cluster)
{
    JumpTable t;
    t.site = site;
    t.opcode = cluster.front().opcode;
    t.kind = cluster.front().kind;
    t.stride = cluster.front().stride;
    uint32_t bank = cluster.front().target >> 16;
    auto stride = uint32_t(t.stride);

    std::map<uint32_t, JumpEntry> entries;
    for (auto &c : cluster)
    {
        auto &e = entries[c.entry];
        e.offset = c.entry;
        // The value in the clean ROM, in case the recording ran on a corrupted one.
        e.target = c.entry + stride <= f.rom.size() ? f.target_at(c.entry, t.stride, c.target >> 16) : c.target;
        e.hits += c.hits;
        e.seen = true;
    }
    auto add_likely = [&](uint32_t o) {
        if (entries.count(o))
            return;
        entries[o] = JumpEntry{ o, f.target_at(o, t.stride, bank), 0, false };
    };
    uint32_t first = entries.begin()->first, last = entries.rbegin()->first;
    for (uint32_t o = first + stride; o < last; o += stride) // gaps between used entries
        if (f.plausible(o, t.stride, bank))
            add_likely(o);
    for (int i = 1; i <= kLikelyReach && first >= stride * uint32_t(i); i++)
    {
        uint32_t o = first - stride * uint32_t(i);
        if (!f.plausible(o, t.stride, bank))
            break;
        add_likely(o);
    }
    for (int i = 1; i <= kLikelyReach; i++)
    {
        uint32_t o = last + stride * uint32_t(i);
        if (!f.plausible(o, t.stride, bank))
            break;
        add_likely(o);
    }
    for (auto &[o, e] : entries)
        t.entries.push_back(e);
    return t;
}

} // namespace

JumpFindings find_jump_tables(const JumpLog &log, std::span<const uint8_t> rom, std::span<const uint8_t> flags,
                              const RomOffsets &offsets)
{
    JumpFindings out;
    Finder f(rom, flags, offsets);
    std::map<uint32_t, std::vector<Found>> per_site;
    struct Unsettled
    {
        JumpKey key;
        JumpSeen seen;
        std::vector<uint32_t> spots;
        int stride;
    };
    std::vector<Unsettled> unsettled;
    for (auto &[key, seen] : log.all())
    {
        if (key.entry != kNoEntry)
        {
            if (key.entry >= rom.size())
                continue;
            int stride = seen.opcode == jumpop::JmlPointer ? 3 : 2;
            per_site[key.site].push_back({ key.entry, key.target, seen.hits, seen.opcode, JumpKind::Direct, stride });
            continue;
        }
        // Through RAM: find the one data-read spot holding the target, with its bank for a
        // long jump, else its low 16 bits.
        auto &spots = f.spots(uint16_t(key.target));
        std::vector<uint32_t> long_spots;
        if (seen.opcode == jumpop::JmlPointer)
            for (auto o : spots)
                if (f.data(o + 2) && rom[o + 2] == (key.target >> 16))
                    long_spots.push_back(o);
        auto &use = long_spots.empty() ? spots : long_spots;
        int stride = long_spots.empty() ? 2 : 3;
        if (use.size() == 1)
            per_site[key.site].push_back({ use[0], key.target, seen.hits, seen.opcode, JumpKind::ThroughRam, stride });
        else if (use.empty())
            out.pointer_jumps++;
        else
            unsettled.push_back({ key, seen, use, stride });
    }
    // A value in several spots: the one spot in line with the site's other entries (the
    // same stride, within a table's reach) is it.
    for (auto &u : unsettled)
    {
        std::optional<uint32_t> pick;
        int matches = 0;
        for (auto o : u.spots)
        {
            bool in_line = false;
            for (auto &e : per_site[u.key.site])
            {
                uint32_t gap = o > e.entry ? o - e.entry : e.entry - o;
                if (e.kind == JumpKind::ThroughRam && e.stride == u.stride && gap && gap <= kTableGap &&
                    gap % uint32_t(u.stride) == 0)
                    in_line = true;
            }
            if (in_line)
            {
                pick = o;
                matches++;
            }
        }
        if (matches == 1)
            per_site[u.key.site].push_back(
                { *pick, u.key.target, u.seen.hits, u.seen.opcode, JumpKind::ThroughRam, u.stride });
        else
            out.ambiguous++;
    }

    for (auto &[site, found] : per_site)
    {
        std::sort(found.begin(), found.end(), [](auto &a, auto &b) {
            return std::tie(a.kind, a.stride, a.entry) < std::tie(b.kind, b.stride, b.entry);
        });
        std::vector<Found> cluster;
        for (auto &e : found)
        {
            if (!cluster.empty())
            {
                auto &prev = cluster.back();
                uint32_t gap = e.entry - prev.entry;
                bool same = e.kind == prev.kind && e.stride == prev.stride &&
                            (gap == 0 || (gap <= kTableGap && gap % uint32_t(e.stride) == 0));
                if (!same)
                {
                    out.tables.push_back(make_table(f, site, cluster));
                    cluster.clear();
                }
            }
            cluster.push_back(e);
        }
        if (!cluster.empty())
            out.tables.push_back(make_table(f, site, cluster));
    }
    std::sort(out.tables.begin(), out.tables.end(),
              [](auto &a, auto &b) { return std::tie(a.site, a.entries.front().offset) < std::tie(b.site, b.entries.front().offset); });
    return out;
}

} // namespace corrupt
