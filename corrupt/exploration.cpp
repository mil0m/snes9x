#include "exploration.h"

#include "nlohmann/json.hpp"
#include "recipe_json.h"

#include <cstdio>

namespace corrupt
{

namespace
{

std::optional<Family> parse_family(const std::string &name)
{
    for (size_t i = 0; i < kFamilies; i++)
        if (name == family_name(Family(i)))
            return Family(i);
    return std::nullopt;
}

std::string base64(const std::string &in)
{
    static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3)
    {
        uint32_t v = uint8_t(in[i]) << 16 | uint8_t(in[i + 1]) << 8 | uint8_t(in[i + 2]);
        out += { t[v >> 18], t[(v >> 12) & 63], t[(v >> 6) & 63], t[v & 63] };
    }
    if (i + 1 == in.size())
    {
        uint32_t v = uint8_t(in[i]) << 16;
        out += { t[v >> 18], t[(v >> 12) & 63], '=', '=' };
    }
    else if (i + 2 == in.size())
    {
        uint32_t v = uint8_t(in[i]) << 16 | uint8_t(in[i + 1]) << 8;
        out += { t[v >> 18], t[(v >> 12) & 63], t[(v >> 6) & 63], '=' };
    }
    return out;
}

std::string escape(const std::string &s)
{
    std::string out;
    for (char c : s)
        switch (c)
        {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out += c;
        }
    return out;
}

std::string pct(double v)
{
    char buf[16];
    snprintf(buf, sizeof buf, "%.0f%%", v * 100);
    return buf;
}

std::string num(double v, const char *fmt = "%.2f")
{
    char buf[32];
    snprintf(buf, sizeof buf, fmt, v);
    return buf;
}

} // namespace

std::string encode_exploration(const Exploration &e)
{
    nlohmann::ordered_json j;
    j["version"] = 1;
    j["name"] = e.name;
    j["game"] = { { "title", e.game.title }, { "sha256", e.game.sha256 }, { "crc32", e.game.crc32 } };
    j["moment"] = e.moment;
    j["made_by"] = e.made_by;
    j["made_at"] = e.made_at;
    j["window"] = e.window;
    j["variants"] = e.variants;
    j["alive"] = e.alive;
    j["visible"] = e.visible;
    j["seconds"] = e.seconds;
    j["finished"] = e.finished;
    auto &fam = j["families"] = nlohmann::ordered_json::object();
    for (size_t i = 0; i < kFamilies; i++)
        if (e.families[i].runs)
            fam[family_name(Family(i))] = { { "runs", e.families[i].runs },
                                            { "alive", e.families[i].alive },
                                            { "visible", e.families[i].visible } };
    auto &cs = j["candidates"] = nlohmann::ordered_json::array();
    for (auto &c : e.candidates)
    {
        nlohmann::ordered_json o;
        o["rank"] = c.rank;
        o["id"] = c.id;
        o["family"] = family_name(c.family);
        o["what"] = c.what;
        o["score"] = c.score.score;
        o["different"] = c.score.different;
        o["lively"] = c.score.lively;
        o["motion"] = c.score.motion;
        o["detail"] = c.score.detail;
        o["new_code"] = c.score.new_code;
        o["moment"] = c.moment_file;
        o["strip"] = c.strip_file;
        if (c.exact)
            o["exact"] = *c.exact;
        o["recipe"] = nlohmann::ordered_json::parse(write_recipe(c.recipe));
        cs.push_back(std::move(o));
    }
    return j.dump(2) + "\n";
}

std::optional<Exploration> decode_exploration(const std::string &json, std::string &error)
{
    auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded() || !j.is_object())
    {
        error = "not JSON";
        return std::nullopt;
    }
    if (j.value("version", 0) != 1)
    {
        error = "made by another version of the explorer";
        return std::nullopt;
    }
    Exploration e;
    auto str = [](const nlohmann::json &o, const char *key) {
        auto it = o.find(key);
        return it != o.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    auto u32 = [](const nlohmann::json &o, const char *key) {
        auto it = o.find(key);
        return it != o.end() && it->is_number_unsigned() ? it->get<uint32_t>() : 0u;
    };
    auto dbl = [](const nlohmann::json &o, const char *key) {
        auto it = o.find(key);
        return it != o.end() && it->is_number() ? it->get<double>() : 0.0;
    };
    e.name = str(j, "name");
    if (auto g = j.find("game"); g != j.end() && g->is_object())
        e.game = { str(*g, "title"), str(*g, "sha256"), str(*g, "crc32") };
    e.moment = str(j, "moment");
    e.made_by = str(j, "made_by");
    e.made_at = str(j, "made_at");
    e.window = u32(j, "window");
    e.variants = u32(j, "variants");
    e.alive = u32(j, "alive");
    e.visible = u32(j, "visible");
    e.seconds = dbl(j, "seconds");
    e.finished = j.value("finished", false);
    if (auto f = j.find("families"); f != j.end() && f->is_object())
        for (auto &[name, v] : f->items())
            if (auto fam = parse_family(name); fam && v.is_object())
                e.families[size_t(*fam)] = { u32(v, "runs"), u32(v, "alive"), u32(v, "visible") };
    if (auto cs = j.find("candidates"); cs != j.end() && cs->is_array())
        for (auto &o : *cs)
        {
            if (!o.is_object())
                continue;
            Candidate c;
            c.rank = u32(o, "rank");
            c.id = u32(o, "id");
            auto fam = parse_family(str(o, "family"));
            auto r = o.find("recipe");
            if (!fam || r == o.end())
                continue;
            auto parsed = parse_recipe(r->dump(), {});
            if (!parsed.recipe)
                continue; // a recipe this version can't read: left out
            c.family = *fam;
            c.recipe = *parsed.recipe;
            c.what = str(o, "what");
            c.score.score = dbl(o, "score");
            c.score.different = dbl(o, "different");
            c.score.lively = dbl(o, "lively");
            c.score.motion = dbl(o, "motion");
            c.score.detail = dbl(o, "detail");
            c.score.new_code = u32(o, "new_code");
            c.score.alive = true;
            c.moment_file = str(o, "moment");
            c.strip_file = str(o, "strip");
            if (auto x = o.find("exact"); x != o.end() && x->is_boolean())
                c.exact = x->get<bool>();
            e.candidates.push_back(std::move(c));
        }
    return e;
}

std::string gallery_html(const Exploration &e, const std::function<std::optional<std::string>(const std::string &)> &read)
{
    std::string h;
    std::string game = e.game.title.empty() ? std::string("Explored") : e.game.title;
    h += "<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
         "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, viewport-fit=cover\">\n"
         "<title>" + escape(game) + " finds</title>\n<meta name=\"claude:viewport-fit-sides\" content=\"cover\">\n";
    h += R"(<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Familjen+Grotesk:wght@500;700&family=JetBrains+Mono:wght@400;600&display=swap">
<style>
/* A contact sheet of runs: each candidate a row of six frames, best first. */
:root {
  --bg: #f3f4f7; --card: #ffffff; --ink: #191c24; --muted: #596072; --line: #dfe2ea;
  --accent: #5b3fd1; --chip: #ebe7fb;
  --display: "Familjen Grotesk", "Avenir Next", "Segoe UI", sans-serif;
  --body: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  --mono: "JetBrains Mono", ui-monospace, Menlo, monospace;
}
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
  --bg: #12131a; --card: #1b1d27; --ink: #e9eaf0; --muted: #a2a8b8; --line: #2c3040; --accent: #a996ff; --chip: #2a2547; color-scheme: dark; } }
:root[data-theme="dark"] {
  --bg: #12131a; --card: #1b1d27; --ink: #e9eaf0; --muted: #a2a8b8; --line: #2c3040; --accent: #a996ff; --chip: #2a2547; color-scheme: dark; }
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--ink); font: 15px/1.55 var(--body); }
main { max-width: 1120px; margin: 0 auto; padding-block: 28px 56px;
  padding-left: max(16px, env(safe-area-inset-left, 0px)); padding-right: max(16px, env(safe-area-inset-right, 0px)); }
.eyebrow { font: 600 12px/1.4 var(--mono); letter-spacing: .08em; text-transform: uppercase; color: var(--accent); margin: 0 0 6px; }
h1 { font: 700 clamp(24px, 4vw, 34px)/1.15 var(--display); margin: 0 0 8px; text-wrap: balance; }
.sub { color: var(--muted); margin: 0 0 18px; max-width: 70ch; }
.kinds { overflow-x: auto; margin: 0 0 26px; }
table { border-collapse: collapse; font: 13px/1.4 var(--mono); font-variant-numeric: tabular-nums; }
th, td { text-align: right; padding: 4px 12px; border-bottom: 1px solid var(--line); }
th { color: var(--muted); font-weight: 600; }
th:first-child, td:first-child { text-align: left; padding-left: 0; }
.cards { display: grid; gap: 14px; }
.card { background: var(--card); border: 1px solid var(--line); border-radius: 10px; padding: 14px 16px; min-width: 0; }
.top { display: flex; flex-wrap: wrap; gap: 6px 10px; align-items: baseline; }
.rank { font: 700 20px/1 var(--display); color: var(--accent); min-width: 2.2em; }
.chip { background: var(--chip); color: var(--ink); border-radius: 999px; padding: 1px 10px; font: 600 12px/1.6 var(--mono); }
.what { color: var(--ink); font-size: 14px; overflow-wrap: anywhere; flex: 1 1 260px; min-width: 0; }
.nums { width: 100%; font: 12px/1.5 var(--mono); color: var(--muted); font-variant-numeric: tabular-nums; }
.strip { width: 100%; max-width: 100%; height: auto; display: block; margin-top: 10px; image-rendering: pixelated; border-radius: 4px; }
details { margin-top: 8px; font-size: 13px; }
summary { cursor: pointer; color: var(--muted); }
summary:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
pre { overflow-x: auto; background: var(--chip); padding: 10px; border-radius: 6px; font: 12px/1.5 var(--mono); }
</style></head><body><main>
)";
    h += "<p class=\"eyebrow\">" + escape(game) + " · explored from a kept moment</p>\n";
    h += "<h1>" + escape(e.moment) + "</h1>\n";
    char line[256];
    snprintf(line, sizeof line, "%u variants of %.0f s each, %u kept running, %u visibly changed something. Explored %s by %s, %.0f min.",
             e.variants, e.window / 60.0, e.alive, e.visible, e.made_at.c_str(), e.made_by.c_str(), e.seconds / 60);
    h += "<p class=\"sub\">" + escape(line) + (e.finished ? "" : " Still exploring: the best so far.") + "</p>\n";
    h += "<div class=\"kinds\"><table><tr><th>Kind</th><th>Tried</th><th>Kept running</th><th>Visible</th></tr>\n";
    for (size_t i = 0; i < kFamilies; i++)
        if (auto &f = e.families[i]; f.runs)
            h += "<tr><td>" + std::string(family_name(Family(i))) + "</td><td>" + std::to_string(f.runs) + "</td><td>" +
                 std::to_string(f.alive) + "</td><td>" + std::to_string(f.visible) + "</td></tr>\n";
    h += "</table></div>\n<div class=\"cards\">\n";
    for (auto &c : e.candidates)
    {
        h += "<div class=\"card\"><div class=\"top\"><span class=\"rank\">#" + std::to_string(c.rank) +
             "</span><span class=\"chip\">" + family_name(c.family) + "</span><span class=\"what\">" + escape(c.what) +
             "</span><span class=\"nums\">score " + num(c.score.score) + " · different " + pct(c.score.different) +
             " · lively " + pct(c.score.lively) + " · new code " + std::to_string(c.score.new_code) + " B" +
             (c.exact ? (*c.exact ? " · plays back exactly" : " · its moment drifts") : "") + "</span></div>\n";
        if (!c.strip_file.empty())
            if (auto png = read(c.strip_file))
                h += "<img class=\"strip\" alt=\"Six pictures across the run of " + escape(c.recipe.name) +
                     "\" src=\"data:image/png;base64," + base64(*png) + "\">\n";
        h += "<details><summary>Recipe</summary><pre>" + escape(write_recipe(c.recipe)) + "</pre></details></div>\n";
    }
    h += "</div></main></body></html>\n";
    return h;
}

} // namespace corrupt
