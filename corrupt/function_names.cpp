#include "function_names.h"

#include "nlohmann/json.hpp"

#include <cstdio>

namespace corrupt
{

using nlohmann::json;

namespace
{

std::string trimmed(std::string s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

} // namespace

std::string encode_names(const NameList &names)
{
    std::string out = "{\"format\": \"snes9x-names\", \"version\": 1, \"functions\": [";
    size_t i = 0;
    for (auto &[entry, name] : names)
    {
        char at[16];
        std::snprintf(at, sizeof at, "0x%06X", entry);
        json e = { { "entry", at }, { "name", name } };
        out += (i++ ? ",\n  " : "\n  ") + e.dump();
    }
    out += names.empty() ? "]}\n" : "\n]}\n";
    return out;
}

std::optional<NameList> decode_names(std::string_view file, std::string &error)
{
    json j = json::parse(file, nullptr, false);
    if (j.is_discarded() || !j.is_object() || j.value("format", std::string()) != "snes9x-names")
    {
        error = "not a names file";
        return std::nullopt;
    }
    if (!j.contains("functions") || !j["functions"].is_array())
    {
        error = "no functions in the file";
        return std::nullopt;
    }
    NameList out;
    for (auto &e : j["functions"])
    {
        if (!e.is_object() || !e.contains("entry") || !e["entry"].is_string() || !e.contains("name") ||
            !e["name"].is_string())
            continue;
        std::string at = e["entry"].get<std::string>();
        if (at.rfind("0x", 0) != 0 && at.rfind("0X", 0) != 0)
            continue;
        char *end = nullptr;
        unsigned long v = std::strtoul(at.c_str() + 2, &end, 16);
        if (end == at.c_str() + 2 || *end || v > 0xFFFFFF)
            continue;
        auto name = trimmed(e["name"].get<std::string>());
        if (!name.empty())
            out[uint32_t(v)] = name;
    }
    error.clear();
    return out;
}

std::string FunctionNames::label(uint32_t entry) const
{
    std::string out;
    auto m = mine.find(entry);
    if (m != mine.end())
        out = m->second;
    auto o = others.find(entry);
    if (o != others.end())
        for (auto &n : o->second)
        {
            if (m != mine.end() && n.name == m->second)
                continue;
            out += (out.empty() ? "" : " · ") + n.name + " (" + n.who + ")";
        }
    return out;
}

void FunctionNames::set_mine(uint32_t entry, const std::string &name)
{
    auto n = trimmed(name);
    if (n.empty())
        mine.erase(entry);
    else
        mine[entry] = n;
}

} // namespace corrupt
