#include "share.h"
#include "recipe_json.h"

#include "nlohmann/json.hpp"

namespace corrupt
{

namespace
{

constexpr const char *kFormat = "snes9x-shared-recipe";
constexpr const char *kDigits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64(const std::vector<uint8_t> &bytes)
{
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (size_t i = 0; i < bytes.size(); i += 3)
    {
        uint32_t v = uint32_t(bytes[i]) << 16;
        if (i + 1 < bytes.size())
            v |= uint32_t(bytes[i + 1]) << 8;
        if (i + 2 < bytes.size())
            v |= bytes[i + 2];
        out += kDigits[(v >> 18) & 63];
        out += kDigits[(v >> 12) & 63];
        out += i + 1 < bytes.size() ? kDigits[(v >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? kDigits[v & 63] : '=';
    }
    return out;
}

std::optional<std::vector<uint8_t>> unbase64(const std::string &text)
{
    if (text.size() % 4)
        return std::nullopt;
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        return c == '+' ? 62 : c == '/' ? 63 : -1;
    };
    std::vector<uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    for (size_t i = 0; i < text.size(); i += 4)
    {
        int v[4];
        int pad = 0;
        for (int k = 0; k < 4; k++)
        {
            char c = text[i + k];
            if (c == '=' && i + 4 == text.size() && k >= 2)
            {
                v[k] = 0;
                pad++;
                continue;
            }
            if (pad || (v[k] = value(c)) < 0)
                return std::nullopt;
        }
        uint32_t n = uint32_t(v[0]) << 18 | uint32_t(v[1]) << 12 | uint32_t(v[2]) << 6 | uint32_t(v[3]);
        out.push_back(uint8_t(n >> 16));
        if (pad < 2)
            out.push_back(uint8_t(n >> 8));
        if (pad < 1)
            out.push_back(uint8_t(n));
    }
    return out;
}

} // namespace

std::string write_shared(const Recipe &recipe, const std::vector<uint8_t> *moment)
{
    Recipe copy = recipe;
    copy.anchor_state.clear();
    for (auto &rule : copy.rules)
        rule.region.clear(); // the receiver doesn't have the sender's names
    nlohmann::ordered_json doc;
    doc["format"] = kFormat;
    doc["version"] = 1;
    doc["recipe"] = nlohmann::ordered_json::parse(write_recipe(copy));
    if (moment && !moment->empty())
        doc["moment"] = base64(*moment);
    return doc.dump(1) + "\n";
}

std::optional<SharedRecipe> read_shared(const std::string &text, std::string &error)
{
    auto doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object())
    {
        error = "not a shared recipe (not JSON)";
        return std::nullopt;
    }
    if (doc.value("format", "") != kFormat)
    {
        error = "not a shared recipe";
        return std::nullopt;
    }
    if (doc.value("version", 0) != 1)
    {
        error = "made by a newer version of the app";
        return std::nullopt;
    }
    if (!doc.contains("recipe") || !doc["recipe"].is_object())
    {
        error = "it has no recipe in it";
        return std::nullopt;
    }
    auto parsed = parse_recipe(doc["recipe"].dump(), {});
    if (!parsed.recipe)
    {
        error = "its recipe is broken: " + (parsed.errors.empty() ? std::string() : parsed.errors[0]);
        return std::nullopt;
    }
    SharedRecipe out{ *parsed.recipe, std::nullopt };
    if (doc.contains("moment"))
    {
        auto bytes = doc["moment"].is_string() ? unbase64(doc["moment"].get<std::string>()) : std::nullopt;
        if (!bytes)
        {
            error = "its start moment is damaged";
            return std::nullopt;
        }
        out.moment = std::move(*bytes);
    }
    return out;
}

} // namespace corrupt
