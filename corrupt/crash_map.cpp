#include "crash_map.h"
#include "nlohmann/json.hpp"

#include <cstdio>

namespace corrupt
{

namespace
{
uint32_t block_of(uint32_t offset)
{
    return offset / CrashMap::kBlock * CrashMap::kBlock;
}

// Past this no game has the memory, so a block there is a broken file, not data.
uint32_t largest_size(Target t)
{
    return t == Target::SRAM ? 0x80000 : uint32_t(nominal_size(t)); // Snes9x's SRAM buffer
}
} // namespace

void CrashMap::add_crash(Target t, uint32_t offset, uint32_t n)
{
    per_target[size_t(t)][block_of(offset)].crashes += n;
}

void CrashMap::add_survival(Target t, uint32_t offset, uint32_t n)
{
    per_target[size_t(t)][block_of(offset)].survivals += n;
}

void CrashMap::merge(const CrashMap &other)
{
    for (size_t t = 0; t < kTargetCount; t++)
        for (auto &[block, c] : other.per_target[t])
        {
            auto &mine = per_target[t][block];
            mine.crashes += c.crashes;
            mine.survivals += c.survivals;
        }
}

void CrashMap::clear()
{
    for (auto &m : per_target)
        m.clear();
}

bool CrashMap::empty() const
{
    for (auto &m : per_target)
        if (!m.empty())
            return false;
    return true;
}

BlockCounts CrashMap::counts(Target t, uint32_t offset) const
{
    auto &m = per_target[size_t(t)];
    auto it = m.find(block_of(offset));
    return it == m.end() ? BlockCounts{} : it->second;
}

size_t CrashMap::hot_blocks() const
{
    size_t n = 0;
    for (auto &m : per_target)
        for (auto &[block, c] : m)
            n += hot_of(c);
    return n;
}

bool HotMask::empty() const
{
    for (auto &v : hot)
        if (!v.empty())
            return false;
    return true;
}

HotMask hot_mask(const CrashMap &map)
{
    HotMask mask;
    for (auto t : kAllTargets)
        for (auto &[block, c] : map.blocks(t))
        {
            if (!CrashMap::hot_of(c))
                continue;
            auto &v = mask.hot[size_t(t)];
            if (v.size() < block + CrashMap::kBlock)
                v.resize(block + CrashMap::kBlock, false);
            for (uint32_t i = 0; i < CrashMap::kBlock; i++)
                v[block + i] = true;
        }
    return mask;
}

std::string crash_map_json(const CrashMap &map)
{
    // Written by hand: a fixed order and one block per line keep git diffs readable.
    std::string out = "{\n  \"version\": 1,\n  \"block\": 16";
    for (auto t : kAllTargets)
    {
        auto &blocks = map.blocks(t);
        if (blocks.empty())
            continue;
        out += ",\n  \"" + std::string(target_name(t)) + "\": {";
        bool first = true;
        for (auto &[block, c] : blocks)
        {
            char line[64];
            snprintf(line, sizeof line, "%s\n    \"0x%04X\": [%u, %u]", first ? "" : ",", block, c.crashes, c.survivals);
            out += line;
            first = false;
        }
        out += "\n  }";
    }
    return out + "\n}\n";
}

CrashMapParse parse_crash_map(const std::string &text)
{
    CrashMapParse out;
    auto doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object())
    {
        out.errors.push_back("not a JSON object");
        return out;
    }
    if (!doc.contains("block") || doc["block"] != CrashMap::kBlock)
    {
        out.errors.push_back("block size isn't 16");
        return out;
    }
    for (auto &[key, value] : doc.items())
    {
        if (key == "version" || key == "block")
            continue;
        auto t = parse_target(key);
        if (!t || !value.is_object())
        {
            out.errors.push_back("unknown memory '" + key + "'");
            continue;
        }
        for (auto &[block, counts] : value.items())
        {
            unsigned long offset = 0;
            size_t used = 0;
            bool ok = block.rfind("0x", 0) == 0 && block.size() > 2;
            if (ok)
                try
                {
                    offset = std::stoul(block.substr(2), &used, 16);
                    ok = used == block.size() - 2 && offset < largest_size(*t);
                }
                catch (...)
                {
                    ok = false;
                }
            bool counts_ok = counts.is_array() && counts.size() == 2 && counts[0].is_number_unsigned() &&
                             counts[1].is_number_unsigned();
            if (!ok || !counts_ok)
            {
                out.errors.push_back(key + " " + block + ": expected \"0x<offset>\": [crashes, survivals], inside the memory");
                continue;
            }
            out.map.add_crash(*t, uint32_t(offset), counts[0].get<uint32_t>());
            out.map.add_survival(*t, uint32_t(offset), counts[1].get<uint32_t>());
        }
    }
    return out;
}

} // namespace corrupt
