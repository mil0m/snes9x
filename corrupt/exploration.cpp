#include "exploration.h"

#include "nlohmann/json.hpp"
#include "recipe_json.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>

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

// 0.62, 1.05, 0.3: two decimals at most, no trailing zeros.
std::string short_num(double v)
{
    std::string s = num(v, "%.2f");
    while (!s.empty() && s.back() == '0')
        s.pop_back();
    if (!s.empty() && s.back() == '.')
        s.pop_back();
    return s;
}


// The three charts (#80), as inline SVG drawn from the theme's tokens.
std::string charts_html(const Exploration &e, const ExploreStats &st)
{
    std::string h = "<section class=\"charts\" aria-label=\"What the exploration did\">\n";
    // 1. What each kind did: one bar per family, its tries split three ways.
    {
        int rows = int(st.kinds.size()), W = 640, label = 64, x0 = 70, bw = 400, rh = 26;
        int H = rows * rh + 6;
        h += "<figure class=\"chart\"><figcaption><b>What each kind did</b> <span>share of its own tries, the most "
             "often visible first</span></figcaption>\n<div class=\"legend\"><span><i class=\"sw visible\"></i>Visibly "
             "changed</span><span><i class=\"sw none\"></i>Nothing showed</span><span><i class=\"sw stopped\"></i>Stopped"
             "</span></div>\n";
        h += "<svg viewBox=\"0 0 " + std::to_string(W) + " " + std::to_string(H) + "\" role=\"img\" aria-label=\"What each "
             "kind of corruption did\">\n";
        for (int r = 0; r < rows; r++)
        {
            auto &k = st.kinds[size_t(r)];
            int y = r * rh + 4;
            char title[200];
            snprintf(title, sizeof title, "%s: %u tried, %u visibly changed, %u showed nothing, %u stopped; %u kept",
                     family_name(k.family), k.runs, k.visible, k.unchanged, k.stopped, k.kept);
            h += "<g><title>" + escape(title) + "</title>";
            h += "<text class=\"lab\" x=\"" + std::to_string(label) + "\" y=\"" + std::to_string(y + 14) +
                 "\" text-anchor=\"end\">" + family_name(k.family) + "</text>";
            double x = x0;
            struct Part
            {
                uint32_t n;
                const char *cls;
            };
            Part parts[] = { { k.visible, "visible" }, { k.unchanged, "none" }, { k.stopped, "stopped" } };
            for (auto &part : parts)
            {
                if (!part.n || !k.runs)
                    continue;
                double w = bw * double(part.n) / k.runs;
                double drawn = std::max(1.0, w - 2); // a 2 px gap between segments
                h += "<rect class=\"" + std::string(part.cls) + "\" x=\"" + num(x, "%.1f") + "\" y=\"" +
                     std::to_string(y + 2) + "\" width=\"" + num(drawn, "%.1f") + "\" height=\"16\" rx=\"3\"/>";
                x += w;
            }
            std::string right = pct(k.visible_share()) + " visible" + (k.kept ? " · " + std::to_string(k.kept) + " kept" : "");
            h += "<text class=\"val\" x=\"" + std::to_string(x0 + bw + 8) + "\" y=\"" + std::to_string(y + 14) + "\">" +
                 right + "</text></g>\n";
        }
        h += "</svg></figure>\n";
    }
    if (!st.detailed)
    {
        h += "<p class=\"note\">This one was explored before the explorer kept every try, so how the scores spread and "
             "when the finds turned up can't be drawn.</p>\n</section>\n";
        return h;
    }
    // 2. How the scores spread: the visible changes' scores, the keep line marked.
    {
        int W = 640, H = 220, x0 = 44, x1 = 628, top = 12, bottom = 176;
        uint32_t most = 1;
        for (auto &b : st.bins)
            most = std::max(most, b.count);
        double slot = double(x1 - x0) / st.bins.size();
        h += std::string("<figure class=\"chart\"><figcaption><b>How the scores spread</b> <span>tries that visibly "
                         "changed something; ") +
             (st.by_find ? "the line is normal play's best: past it, they look different beyond normal play"
                         : "from the line up they can be kept") +
             "</span></figcaption>\n";
        h += "<svg viewBox=\"0 0 " + std::to_string(W) + " " + std::to_string(H) + "\" role=\"img\" aria-label=\"How the "
             "scores spread\">\n";
        for (double f : { 0.0, 0.5, 1.0 })
        {
            double y = bottom - f * (bottom - top);
            h += "<line class=\"grid\" x1=\"" + std::to_string(x0) + "\" x2=\"" + std::to_string(x1) + "\" y1=\"" +
                 num(y, "%.1f") + "\" y2=\"" + num(y, "%.1f") + "\"/><text class=\"tick\" x=\"" + std::to_string(x0 - 6) +
                 "\" y=\"" + num(y + 4, "%.1f") + "\" text-anchor=\"end\">" + std::to_string(uint32_t(std::lround(f * most))) +
                 "</text>";
        }
        for (size_t i = 0; i < st.bins.size(); i++)
        {
            auto &b = st.bins[i];
            double bh = (bottom - top) * double(b.count) / most;
            double x = x0 + i * slot + 3;
            std::string range = std::isinf(b.hi) ? short_num(b.lo) + " and up" : short_num(b.lo) + "-" + short_num(b.hi);
            h += "<g><title>" + range + ": " + std::to_string(b.count) + (b.count == 1 ? " try" : " tries") + "</title>";
            if (b.count)
                h += "<rect class=\"" + std::string(b.kept_side ? "visible" : "below") + "\" x=\"" + num(x, "%.1f") +
                     "\" y=\"" + num(bottom - bh, "%.1f") + "\" width=\"" + num(slot - 6, "%.1f") + "\" height=\"" +
                     num(std::max(1.0, bh), "%.1f") + "\" rx=\"3\"/>";
            h += "<rect class=\"hit\" x=\"" + num(x, "%.1f") + "\" y=\"" + std::to_string(top) + "\" width=\"" +
                 num(slot - 6, "%.1f") + "\" height=\"" + std::to_string(bottom - top) + "\"/>";
            h += "<text class=\"tick\" x=\"" + num(x + (slot - 6) / 2, "%.1f") + "\" y=\"" + std::to_string(bottom + 16) +
                 "\" text-anchor=\"middle\">" + (std::isinf(b.hi) ? short_num(b.lo) + "+" : short_num(b.lo)) + "</text></g>\n";
            if (i > 0 && b.kept_side && !st.bins[i - 1].kept_side)
            {
                double lx = x0 + i * slot;
                h += "<line class=\"keep\" x1=\"" + num(lx, "%.1f") + "\" x2=\"" + num(lx, "%.1f") + "\" y1=\"" +
                     std::to_string(top) + "\" y2=\"" + std::to_string(bottom) + "\"/><text class=\"tick\" x=\"" +
                     num(lx + 4, "%.1f") + "\" y=\"" + std::to_string(top + 10) + "\">keep line " + short_num(st.keep_line) +
                     "</text>\n";
            }
        }
        h += "<text class=\"tick\" x=\"" + std::to_string((x0 + x1) / 2) + "\" y=\"" + std::to_string(H - 6) +
             "\" text-anchor=\"middle\">score</text></svg>\n";
        h += "<p class=\"note\">" + std::to_string(st.zero_scores) + (st.zero_scores == 1 ? " try" : " tries") +
             " ran and changed nothing you could see (score 0); they aren't drawn.</p></figure>\n";
    }
    // 3. When the good finds turned up: each try at or above the keep line, and the best so far.
    {
        int W = 640, H = 220, x0 = 44, x1 = 628, top = 12, bottom = 176;
        double ymax = 3;
        for (auto &f : st.finds)
            ymax = std::max(ymax, f.value * 1.08);
        if (!st.best.empty())
            ymax = std::max(ymax, st.best.back().second * 1.08);
        double n = std::max<uint32_t>(1, st.tried);
        auto X = [&](double at) { return x0 + (x1 - x0) * at / n; };
        auto Y = [&](double v) { return bottom - (bottom - top) * v / ymax; };
        double line = st.by_find ? 1.0 : kMinScore;
        h += std::string("<figure class=\"chart wide\"><figcaption><b>When the good finds turned up</b> <span>") +
             (st.by_find ? "each find by its strength (1: as different as normal play gets; +1 for each way it acts "
                           "different), by when it was tried; the line is the best so far"
                         : "each try that scored 0.3 or more, by when it was tried; the line is the best so far") +
             "</span></figcaption>\n";
        h += "<div class=\"legend\"><span><i class=\"dot kept\"></i>Kept</span><span><i class=\"dot\"></i>Good, not in "
             "the best 24</span><span><i class=\"step\"></i>Best so far</span></div>\n";
        h += "<svg viewBox=\"0 0 " + std::to_string(W) + " " + std::to_string(H) + "\" role=\"img\" aria-label=\"When "
             "the good finds turned up\">\n";
        for (int v = 0; v <= int(ymax); v++)
        {
            double y = Y(v);
            h += "<line class=\"grid\" x1=\"" + std::to_string(x0) + "\" x2=\"" + std::to_string(x1) + "\" y1=\"" +
                 num(y, "%.1f") + "\" y2=\"" + num(y, "%.1f") + "\"/><text class=\"tick\" x=\"" + std::to_string(x0 - 6) +
                 "\" y=\"" + num(y + 4, "%.1f") + "\" text-anchor=\"end\">" + std::to_string(v) + "</text>";
        }
        h += "<line class=\"keep\" x1=\"" + std::to_string(x0) + "\" x2=\"" + std::to_string(x1) + "\" y1=\"" +
             num(Y(line), "%.1f") + "\" y2=\"" + num(Y(line), "%.1f") + "\"/>\n";
        if (!st.best.empty())
        {
            std::string d = "M" + num(X(st.best[0].first), "%.1f") + "," + num(Y(st.best[0].second), "%.1f");
            for (size_t i = 1; i < st.best.size(); i++)
                d += " H" + num(X(st.best[i].first), "%.1f") + " V" + num(Y(st.best[i].second), "%.1f");
            d += " H" + num(X(n), "%.1f");
            h += "<path class=\"best\" d=\"" + d + "\"/>\n";
        }
        for (auto &f : st.finds)
        {
            char title[120];
            snprintf(title, sizeof title, "#%u (%s): %s %.2f%s", f.at + 1, family_name(e.tried[f.at].family),
                     st.by_find ? "find" : "score", f.value, f.kept ? ", kept" : "");
            h += "<g><title>" + escape(title) + "</title><circle class=\"hit\" cx=\"" + num(X(f.at), "%.1f") + "\" cy=\"" +
                 num(Y(f.value), "%.1f") + "\" r=\"9\"/><circle class=\"" + std::string(f.kept ? "find kept" : "find") +
                 "\" cx=\"" + num(X(f.at), "%.1f") + "\" cy=\"" + num(Y(f.value), "%.1f") + "\" r=\"4.5\"/></g>\n";
        }
        for (double f : { 0.0, 0.5, 1.0 })
            h += "<text class=\"tick\" x=\"" + num(X(f * n), "%.1f") + "\" y=\"" + std::to_string(bottom + 16) +
                 "\" text-anchor=\"" + (f == 0 ? "start" : f == 1 ? "end" : "middle") + "\">" +
                 std::to_string(uint32_t(std::lround(f * n))) + "</text>";
        h += "<text class=\"tick\" x=\"" + std::to_string((x0 + x1) / 2) + "\" y=\"" + std::to_string(H - 6) +
             "\" text-anchor=\"middle\">tries, in the order tried</text></svg></figure>\n";
    }
    h += "</section>\n";
    return h;
}

} // namespace

bool NormalBest::add(const RunScore &s, const Signals &g)
{
    if (!s.alive)
        return false;
    bool rose = s.score > score || g.new_functions > new_functions || g.silenced > silenced || s.new_code > new_code;
    score = std::max(score, s.score);
    new_functions = std::max(new_functions, g.new_functions);
    silenced = std::max(silenced, g.silenced);
    new_code = std::max(new_code, s.new_code);
    return rose;
}

Verdict judge(const RunScore &s, const Signals &g, const NormalBest &best)
{
    Verdict v;
    double line = best.keep_line();
    v.looks = s.alive ? s.score / line : 0;
    bool looks_past = s.alive && s.score >= kMinScore && (best.runs == 0 || s.score > best.score);
    if (best.runs > 0 && s.alive && s.different >= kVisible)
    {
        if (g.new_functions > best.new_functions)
            v.acts |= acts::NewFunctions;
        if (g.silenced > best.silenced)
            v.acts |= acts::Silenced;
        if (s.new_code > best.new_code)
            v.acts |= acts::NewCode;
    }
    v.kept = looks_past || v.acts != 0;
    v.find = v.looks + std::popcount(v.acts);
    if (!v.kept) // 1 or more means kept
        v.find = std::min(v.find, 0.999);
    return v;
}

std::string acts_text(uint8_t a, const Signals &g, const RunScore &s)
{
    std::vector<std::string> parts;
    auto count = [](uint32_t n, const char *one, const char *many) {
        return n == 1 ? std::string(one) : std::to_string(n) + many;
    };
    if (a & acts::NewFunctions)
        parts.push_back(count(g.new_functions, "a function normal play never calls",
                              " functions normal play never calls"));
    if (a & acts::Silenced)
        parts.push_back(count(g.silenced, "a function normal play always calls went quiet",
                              " functions normal play always calls went quiet"));
    if (a & acts::NewCode)
        parts.push_back(std::to_string(s.new_code) + " bytes of code normal play never ran");
    std::string out;
    for (size_t i = 0; i < parts.size(); i++)
        out += (i ? "; " : "") + parts[i];
    return out;
}

std::string normal_text(const NormalBest &n)
{
    char buf[320];
    snprintf(buf, sizeof buf,
             "Normal play: %u runs without corruption, most with one button press moved. Their best score was %s, so a "
             "find has to score more than %s, or act in a way normal play doesn't (the most normal play reached: new "
             "functions %u, gone quiet %u, new code %u bytes).",
             n.runs + n.later, short_num(n.score).c_str(), short_num(n.keep_line()).c_str(), n.new_functions, n.silenced,
             n.new_code);
    std::string out = buf;
    if (n.later)
    {
        std::string kept = n.later_kept ? std::to_string(n.later_kept) + " would have been kept (" +
                                              short_num(100.0 * n.later_kept / n.later) + "%)"
                                        : std::string("none would have been kept");
        snprintf(buf, sizeof buf,
                 " %u of them were played between the corruptions, each checked against normal play so far first: "
                 "%s, about how often a find here is just normal play.",
                 n.later, kept.c_str());
        out += buf;
    }
    return out;
}

std::string why_kept(const Candidate &c, const NormalBest &n)
{
    std::string out;
    if (c.score.score >= kMinScore && c.score.score > n.score)
        out = "Looks different beyond normal play: score " + short_num(c.score.score) + ", normal play's best " +
              short_num(n.score) + ".";
    if (c.verdict.acts)
        out += (out.empty() ? "" : " ") + std::string("Acts different: ") + acts_text(c.verdict.acts, c.measured, c.score) + ".";
    return out;
}

ExploreStats stats_of(const Exploration &e)
{
    ExploreStats st;
    std::array<KindStats, kFamilies> kinds{};
    for (size_t i = 0; i < kFamilies; i++)
        kinds[i].family = Family(i);
    st.detailed = !e.tried.empty();
    std::vector<uint32_t> kept_ids;
    for (auto &c : e.candidates)
        kept_ids.push_back(c.id);
    for (auto &c : e.candidates)
        kinds[size_t(c.family)].kept++;
    st.by_find = e.normal.runs > 0;
    st.keep_line = e.normal.keep_line();
    if (st.detailed)
    {
        // The keep line is an edge: a bin is wholly on one side of it.
        std::vector<double> edges = { 0, 0.05, 0.1, 0.2, 0.3, 0.5, 1, 1.5, 2, 3 };
        auto near = std::find_if(edges.begin(), edges.end(), [&](double x) { return std::abs(x - st.keep_line) < 1e-9; });
        if (near == edges.end())
        {
            edges.insert(std::upper_bound(edges.begin(), edges.end(), st.keep_line), st.keep_line);
            // Drop an edge too close to it to read.
            for (size_t i = 1; i < edges.size(); i++)
                if (edges[i] != st.keep_line && std::abs(edges[i] - st.keep_line) < 0.1 && edges[i] > 0)
                {
                    edges.erase(edges.begin() + long(i));
                    break;
                }
        }
        for (size_t i = 0; i < edges.size(); i++)
        {
            double hi = i + 1 < edges.size() ? edges[i + 1] : std::numeric_limits<double>::infinity();
            st.bins.push_back({ edges[i], hi, 0, edges[i] >= st.keep_line - 1e-9 });
        }
        double best = -1;
        for (uint32_t i = 0; i < e.tried.size(); i++)
        {
            auto &t = e.tried[i];
            auto &k = kinds[size_t(t.family)];
            k.runs++;
            if (t.outcome == Tried::Stopped)
                k.stopped++;
            else if (t.outcome == Tried::Unchanged)
                k.unchanged++;
            else
                k.visible++;
            if (t.outcome == Tried::Unchanged)
                st.zero_scores++;
            if (t.outcome == Tried::Visible)
                for (auto &bin : st.bins)
                    if (t.score >= bin.lo && t.score < bin.hi)
                    {
                        bin.count++;
                        break;
                    }
            double value = st.by_find ? t.find : t.score;
            bool find = t.outcome != Tried::Stopped && (st.by_find ? t.find >= 1 : t.score >= kMinScore);
            if (find)
                st.finds.push_back({ i, value, std::find(kept_ids.begin(), kept_ids.end(), i) != kept_ids.end() });
            if (t.outcome == Tried::Visible && value > best)
            {
                best = value;
                st.best.push_back({ i, value });
            }
        }
        st.tried = uint32_t(e.tried.size());
    }
    else
    {
        for (size_t i = 0; i < kFamilies; i++)
        {
            auto &f = e.families[i];
            kinds[i].runs = f.runs;
            kinds[i].stopped = f.runs - std::min(f.runs, f.alive);
            kinds[i].visible = f.visible;
            kinds[i].unchanged = f.alive - std::min(f.alive, f.visible);
        }
        st.tried = e.variants;
    }
    for (auto &k : kinds)
        if (k.runs)
            st.kinds.push_back(k);
    // The most often visible first; then the fewer stops.
    std::stable_sort(st.kinds.begin(), st.kinds.end(), [](const KindStats &a, const KindStats &b) {
        if (a.visible_share() != b.visible_share())
            return a.visible_share() > b.visible_share();
        return double(a.stopped) / a.runs < double(b.stopped) / b.runs;
    });
    return st;
}

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
    if (e.normal.runs)
        j["normal"] = { { "runs", e.normal.runs },
                        { "score", e.normal.score },
                        { "new_functions", e.normal.new_functions },
                        { "silenced", e.normal.silenced },
                        { "new_code", e.normal.new_code },
                        { "later", e.normal.later },
                        { "later_kept", e.normal.later_kept } };
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
        if (e.normal.runs)
        {
            o["find"] = c.verdict.find;
            o["looks"] = c.verdict.looks;
            auto a = nlohmann::ordered_json::array();
            if (c.verdict.acts & acts::NewFunctions)
                a.push_back("new functions");
            if (c.verdict.acts & acts::Silenced)
                a.push_back("silenced");
            if (c.verdict.acts & acts::NewCode)
                a.push_back("new code");
            o["acts"] = a;
            o["new_functions"] = c.measured.new_functions;
            o["silenced"] = c.measured.silenced;
            o["apart"] = c.measured.apart;
            o["new_colours"] = c.measured.new_colours;
        }
        o["moment"] = c.moment_file;
        o["strip"] = c.strip_file;
        if (c.exact)
            o["exact"] = *c.exact;
        o["recipe"] = nlohmann::ordered_json::parse(write_recipe(c.recipe));
        cs.push_back(std::move(o));
    }
    // Every try (#80): [family, outcome, score x 1000, find x 1000 (#82)], one line, in the order tried.
    auto tried = nlohmann::ordered_json::array();
    for (auto &t : e.tried)
        if (e.normal.runs)
            tried.push_back({ int(t.family), int(t.outcome), int(std::lround(t.score * 1000)), int(std::lround(t.find * 1000)) });
        else
            tried.push_back({ int(t.family), int(t.outcome), int(std::lround(t.score * 1000)) });
    std::string text = j.dump(2);
    if (!e.tried.empty())
    {
        // Compact: 50,000 tries stay well under a megabyte.
        text.pop_back(); // the closing brace
        while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
            text.pop_back();
        text += ",\n  \"tried\": " + tried.dump() + "\n}";
    }
    return text + "\n";
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
    if (auto n = j.find("normal"); n != j.end() && n->is_object())
        e.normal = { u32(*n, "runs"),     dbl(*n, "score"),  u32(*n, "new_functions"), u32(*n, "silenced"),
                     u32(*n, "new_code"), u32(*n, "later"), u32(*n, "later_kept") };
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
            c.verdict.find = dbl(o, "find");
            c.verdict.looks = dbl(o, "looks");
            if (auto a = o.find("acts"); a != o.end() && a->is_array())
                for (auto &x : *a)
                {
                    std::string name = x.is_string() ? x.get<std::string>() : std::string();
                    c.verdict.acts |= name == "new functions" ? acts::NewFunctions
                                      : name == "silenced"    ? acts::Silenced
                                      : name == "new code"    ? acts::NewCode
                                                              : 0;
                }
            c.verdict.kept = true;
            c.measured.new_functions = u32(o, "new_functions");
            c.measured.silenced = u32(o, "silenced");
            c.measured.apart = dbl(o, "apart");
            c.measured.new_colours = dbl(o, "new_colours");
            c.moment_file = str(o, "moment");
            c.strip_file = str(o, "strip");
            if (auto x = o.find("exact"); x != o.end() && x->is_boolean())
                c.exact = x->get<bool>();
            e.candidates.push_back(std::move(c));
        }
    if (auto ts = j.find("tried"); ts != j.end() && ts->is_array())
        for (auto &t : *ts)
        {
            if (!t.is_array() || t.size() < 3 || t.size() > 4)
                continue;
            bool numbers = true;
            for (auto &x : t)
                numbers &= x.is_number_integer();
            if (!numbers)
                continue;
            int f = t[0].get<int>(), o = t[1].get<int>();
            if (f < 0 || f >= int(kFamilies) || o < 0 || o > 2)
                continue;
            e.tried.push_back({ Family(f), Tried::Outcome(o), float(t[2].get<int>() / 1000.0),
                                t.size() == 4 ? float(t[3].get<int>() / 1000.0) : 0.0f });
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
  --c-visible: #2a78d6; --c-stopped: #eb6834; --c-none: #d7d6cf; --c-below: #b9b8b1; --grid: #e1e0d9; --axis: #898781;
  --display: "Familjen Grotesk", "Avenir Next", "Segoe UI", sans-serif;
  --body: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  --mono: "JetBrains Mono", ui-monospace, Menlo, monospace;
}
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
  --bg: #12131a; --card: #1b1d27; --ink: #e9eaf0; --muted: #a2a8b8; --line: #2c3040; --accent: #a996ff; --chip: #2a2547;
  --c-visible: #3987e5; --c-stopped: #d95926; --c-none: #3a3c47; --c-below: #555866; --grid: #2c2f3b; --axis: #8a8f9e; color-scheme: dark; } }
:root[data-theme="dark"] {
  --bg: #12131a; --card: #1b1d27; --ink: #e9eaf0; --muted: #a2a8b8; --line: #2c3040; --accent: #a996ff; --chip: #2a2547;
  --c-visible: #3987e5; --c-stopped: #d95926; --c-none: #3a3c47; --c-below: #555866; --grid: #2c2f3b; --axis: #8a8f9e; color-scheme: dark; }
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
.charts { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 420px), 1fr)); gap: 14px; margin: 0 0 22px; }
.chart { margin: 0; background: var(--card); border: 1px solid var(--line); border-radius: 10px; padding: 12px 14px; min-width: 0; }
.chart.wide { grid-column: 1 / -1; }
.chart figcaption { font-size: 14px; margin: 0 0 6px; }
.chart figcaption span { color: var(--muted); font-size: 13px; }
.chart svg { width: 100%; height: auto; display: block; overflow: visible; }
.chart text { font: 12px var(--mono); fill: var(--muted); }
.chart text.lab { fill: var(--ink); }
.chart text.val { fill: var(--ink); font-variant-numeric: tabular-nums; }
.chart .visible { fill: var(--c-visible); } .chart .stopped { fill: var(--c-stopped); }
.chart .none { fill: var(--c-none); } .chart .below { fill: var(--c-below); }
.chart .grid { stroke: var(--grid); stroke-width: 1; }
.chart .keep { stroke: var(--axis); stroke-width: 1.5; stroke-dasharray: 4 3; }
.chart .best { fill: none; stroke: var(--ink); stroke-width: 2; opacity: .7; }
.chart .find { fill: var(--card); stroke: var(--c-visible); stroke-width: 2; }
.chart .find.kept { fill: var(--c-visible); }
.chart .hit { fill: transparent; }
.chart g:hover rect:not(.hit), .chart g:hover .find { opacity: .75; }
.legend { display: flex; flex-wrap: wrap; gap: 4px 14px; font-size: 12px; color: var(--muted); margin: 0 0 6px; }
.legend span { display: inline-flex; align-items: center; gap: 6px; }
.sw { width: 12px; height: 12px; border-radius: 3px; display: inline-block; }
.sw.visible { background: var(--c-visible); } .sw.none { background: var(--c-none); } .sw.stopped { background: var(--c-stopped); }
.dot { width: 10px; height: 10px; border-radius: 50%; border: 2px solid var(--c-visible); display: inline-block; }
.dot.kept { background: var(--c-visible); }
.step { width: 16px; height: 2px; background: var(--ink); opacity: .7; display: inline-block; }
.note { color: var(--muted); font-size: 13px; margin: 6px 0 0; }
.why { margin: 6px 0 0; font-size: 14px; }
pre { overflow-x: auto; background: var(--chip); padding: 10px; border-radius: 6px; font: 12px/1.5 var(--mono); }
</style></head><body><main>
)";
    h += "<p class=\"eyebrow\">" + escape(game) + " · explored from a kept moment</p>\n";
    h += "<h1>" + escape(e.moment) + "</h1>\n";
    char line[256];
    snprintf(line, sizeof line, "%u variants of %.0f s each, %u kept running, %u visibly changed something. Explored %s by %s, %.0f min.",
             e.variants, e.window / 60.0, e.alive, e.visible, e.made_at.c_str(), e.made_by.c_str(), e.seconds / 60);
    h += "<p class=\"sub\">" + escape(line) + (e.finished ? "" : " Still exploring: the best so far.") + "</p>\n";
    if (e.normal.runs)
        h += "<p class=\"sub\">" + escape(normal_text(e.normal)) + "</p>\n";
    auto st = stats_of(e);
    h += charts_html(e, st);
    // The first chart as a table, for reading the numbers.
    h += "<div class=\"kinds\"><table><tr><th>Kind</th><th>Tried</th><th>Visibly changed</th><th>Nothing showed</th>"
         "<th>Stopped</th><th>Kept</th></tr>\n";
    for (auto &k : st.kinds)
        h += "<tr><td>" + std::string(family_name(k.family)) + "</td><td>" + std::to_string(k.runs) + "</td><td>" +
             std::to_string(k.visible) + "</td><td>" + std::to_string(k.unchanged) + "</td><td>" + std::to_string(k.stopped) +
             "</td><td>" + std::to_string(k.kept) + "</td></tr>\n";
    h += "</table></div>\n<div class=\"cards\">\n";
    for (auto &c : e.candidates)
    {
        h += "<div class=\"card\"><div class=\"top\"><span class=\"rank\">#" + std::to_string(c.rank) +
             "</span><span class=\"chip\">" + family_name(c.family) + "</span><span class=\"what\">" + escape(c.what) +
             "</span><span class=\"nums\">" + (e.normal.runs ? "find " + num(c.verdict.find) + " · " : std::string()) +
             "score " + num(c.score.score) + " · different " + pct(c.score.different) +
             " · lively " + pct(c.score.lively) + " · new code " + std::to_string(c.score.new_code) + " B" +
             (c.exact ? (*c.exact ? " · plays back exactly" : " · its moment drifts") : "") + "</span></div>\n";
        if (e.normal.runs)
            h += "<p class=\"why\">" + escape(why_kept(c, e.normal)) + "</p>\n";
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
