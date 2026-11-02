#include "patches.h"

#include <map>

namespace corrupt
{

namespace
{

using Bytes = std::map<std::pair<size_t, uint32_t>, uint8_t>; // (target, offset) -> value

Bytes flatten(const std::vector<Patch> &patches)
{
    Bytes out;
    for (auto &p : patches)
        for (uint32_t i = 0; i < p.bytes.size(); i++)
            out[{ size_t(p.target), p.at + i }] = p.bytes[i];
    return out;
}

std::vector<Patch> runs(const Bytes &bytes)
{
    std::vector<Patch> out;
    for (auto &[key, value] : bytes)
    {
        auto t = kAllTargets[key.first];
        if (!out.empty() && out.back().target == t && out.back().at + out.back().bytes.size() == key.second)
            out.back().bytes.push_back(value);
        else
            out.push_back({ t, key.second, { value } });
    }
    return out;
}

} // namespace

void add_patch_bytes(std::vector<Patch> &patches, Target t, const std::vector<std::pair<uint32_t, uint8_t>> &bytes)
{
    auto all = flatten(patches);
    for (auto &[offset, value] : bytes)
        all[{ size_t(t), offset }] = value;
    patches = runs(all);
}

void remove_patch_bytes(std::vector<Patch> &patches, Target t, const std::vector<uint32_t> &offsets)
{
    auto all = flatten(patches);
    for (auto offset : offsets)
        all.erase({ size_t(t), offset });
    patches = runs(all);
}

std::optional<uint8_t> patch_byte(const std::vector<Patch> &patches, Target t, uint32_t offset)
{
    for (auto &p : patches)
        if (p.target == t && offset >= p.at && offset - p.at < p.bytes.size())
            return p.bytes[offset - p.at];
    return std::nullopt;
}

size_t patched_bytes(const std::vector<Patch> &patches)
{
    size_t n = 0;
    for (auto &p : patches)
        n += p.bytes.size();
    return n;
}

} // namespace corrupt
