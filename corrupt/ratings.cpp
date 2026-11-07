#include "ratings.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>

namespace corrupt
{

std::string encode_ratings(const Ratings &r)
{
    nlohmann::ordered_json j;
    j["version"] = 1;
    auto &list = j["ratings"] = nlohmann::ordered_json::array();
    for (auto &x : r)
        list.push_back({ { "id", x.id }, { "good", x.good }, { "at", x.at }, { "find", x.find }, { "score", x.score } });
    return j.dump(2) + "\n";
}

std::optional<Ratings> decode_ratings(const std::string &json, std::string &error)
{
    auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("ratings") || !j["ratings"].is_array())
    {
        error = "not a ratings file";
        return std::nullopt;
    }
    if (j.value("version", 0) != 1)
    {
        error = "made by another version";
        return std::nullopt;
    }
    Ratings out;
    for (auto &x : j["ratings"])
    {
        if (!x.is_object() || !x.contains("id") || !x["id"].is_number_unsigned() || !x.contains("good") ||
            !x["good"].is_boolean())
            continue; // one bad entry doesn't lose the rest
        Rating r;
        r.id = x["id"].get<uint32_t>();
        r.good = x["good"].get<bool>();
        r.at = x.value("at", std::string());
        r.find = x.value("find", 0.0);
        r.score = x.value("score", 0.0);
        if (!rating_of(out, r.id))
            out.push_back(std::move(r));
    }
    return out;
}

const Rating *rating_of(const Ratings &r, uint32_t id)
{
    for (auto &x : r)
        if (x.id == id)
            return &x;
    return nullptr;
}

void set_rating(Ratings &r, const Candidate &c, std::optional<bool> good, const std::string &at)
{
    std::erase_if(r, [&](const Rating &x) { return x.id == c.id; });
    if (!good)
        return;
    Rating x;
    x.id = c.id;
    x.good = *good;
    x.at = at;
    x.find = c.verdict.find;
    x.score = c.score.score;
    r.push_back(std::move(x));
}

std::string ratings_text(const PeoplesRatings &all, uint32_t id, const std::string &me)
{
    std::vector<std::string> parts;
    auto say = [&](const std::string &who, const Rating &x) {
        parts.push_back(who + ": " + (x.good ? "good find" : "not a find"));
    };
    if (auto it = all.find(me); it != all.end())
        if (auto x = rating_of(it->second, id))
            say("You", *x);
    for (auto &[who, list] : all)
        if (who != me)
            if (auto x = rating_of(list, id))
                say(who, *x);
    std::string out;
    for (auto &p : parts)
        out += (out.empty() ? "" : " ") + p + ".";
    return out;
}

namespace
{

// One person's rating of one candidate, with what the explorer measured of it.
struct Row
{
    std::string who;
    bool good = false;
    const Candidate *c = nullptr; // empty: it left the top after it was rated
    bool normal = false;          // its exploration measured normal play (#82)
    double find = 0, score = 0;
};

// The chance a good find's value ranks above a not-find's (ties count half): 0.5 is a coin.
std::optional<double> auc(const std::vector<std::pair<double, bool>> &v)
{
    std::vector<double> good, bad;
    for (auto [x, g] : v)
        (g ? good : bad).push_back(x);
    if (good.empty() || bad.empty())
        return std::nullopt;
    double wins = 0;
    for (double a : good)
        for (double b : bad)
            wins += a > b ? 1 : a == b ? 0.5 : 0;
    return wins / (double(good.size()) * double(bad.size()));
}

std::string fmt(const char *f, double a)
{
    char buf[64];
    snprintf(buf, sizeof buf, f, a);
    return buf;
}

} // namespace

std::string ratings_report(const std::vector<RatedExploration> &list)
{
    std::vector<Row> rows;
    std::map<std::string, std::pair<int, int>> people; // good, not
    // Two people on the same candidate: agreed, of.
    int agree = 0, pairs = 0;
    size_t rated_explorations = 0;
    for (auto &x : list)
    {
        bool any = false;
        for (auto &[who, ratings] : x.ratings)
            for (auto &r : ratings)
            {
                Row row;
                row.who = who;
                row.good = r.good;
                row.normal = x.exploration.normal.runs > 0;
                row.find = r.find;
                row.score = r.score;
                for (auto &c : x.exploration.candidates)
                    if (c.id == r.id)
                        row.c = &c;
                rows.push_back(row);
                (r.good ? people[who].first : people[who].second)++;
                any = true;
            }
        rated_explorations += any;
        std::set<uint32_t> ids;
        for (auto &[who, ratings] : x.ratings)
            for (auto &r : ratings)
                ids.insert(r.id);
        for (auto id : ids)
        {
            std::vector<bool> says;
            for (auto &[who, ratings] : x.ratings)
                if (auto r = rating_of(ratings, id))
                    says.push_back(r->good);
            for (size_t a = 0; a < says.size(); a++)
                for (size_t b = a + 1; b < says.size(); b++)
                {
                    pairs++;
                    agree += says[a] == says[b];
                }
        }
    }

    std::string out;
    if (rows.empty())
        return "No ratings yet: rate candidates on the Explored page (Good Find / Not a Find).\n";
    int good = 0;
    for (auto &r : rows)
        good += r.good;
    out += "Ratings: " + std::to_string(rows.size()) + " in " + std::to_string(rated_explorations) +
           " exploration(s), " + std::to_string(good) + " good finds, " + std::to_string(int(rows.size()) - good) +
           " not.\n";
    for (auto &[who, n] : people)
        out += "  " + who + ": " + std::to_string(n.first) + " good, " + std::to_string(n.second) + " not\n";
    if (pairs)
        out += "Two people on the same candidate: agreed " + std::to_string(agree) + " of " + std::to_string(pairs) +
               " times (" + fmt("%.0f", 100.0 * agree / pairs) + "%): no measure can do better than people agree.\n";
    else
        out += "Nobody rated a candidate someone else rated, so there's no agreement to compare with yet.\n";

    // How each measure separates good finds from the rest.
    struct Measure
    {
        const char *name;
        std::function<std::optional<double>(const Row &)> get;
    };
    auto cand = [](auto f) {
        return [f](const Row &r) -> std::optional<double> {
            if (!r.c)
                return std::nullopt;
            return f(*r.c);
        };
    };
    auto normal_only = [](auto f) {
        return [f](const Row &r) -> std::optional<double> {
            if (!r.c || !r.normal)
                return std::nullopt;
            return f(*r.c);
        };
    };
    std::vector<Measure> measures = {
        { "find (the rank)", [](const Row &r) -> std::optional<double> {
             if (!r.normal)
                 return std::nullopt;
             return r.find;
         } },
        { "score", [](const Row &r) -> std::optional<double> { return r.score; } },
        { "visible change", cand([](const Candidate &c) { return std::min(1.0, c.score.different); }) },
        { "looks like a game", cand([](const Candidate &c) { return c.score.detail; }) },
        { "moving", cand([](const Candidate &c) { return c.score.motion; }) },
        { "new code", cand([](const Candidate &c) { return double(c.score.new_code); }) },
        { "new functions", normal_only([](const Candidate &c) { return double(c.measured.new_functions); }) },
        { "silenced", normal_only([](const Candidate &c) { return double(c.measured.silenced); }) },
        { "ways it acts different", normal_only([](const Candidate &c) { return double(std::popcount(c.verdict.acts)); }) },
        { "apart (nearest normal run)", normal_only([](const Candidate &c) { return c.measured.apart; }) },
        { "new colours", normal_only([](const Candidate &c) { return c.measured.new_colours; }) },
    };
    out += "\nHow well each measure picks the good finds (AUC: the chance a good find ranks above a not-find;\n"
           "0.5 is a coin, 1 perfect):\n";
    for (auto &m : measures)
    {
        std::vector<std::pair<double, bool>> v;
        for (auto &r : rows)
            if (auto x = m.get(r))
                v.push_back({ *x, r.good });
        auto a = auc(v);
        char line[160];
        if (a)
            snprintf(line, sizeof line, "  %-28s %.2f  (%zu ratings)\n", m.name, *a, v.size());
        else
            snprintf(line, sizeof line, "  %-28s  -    (%zu ratings: needs both kinds)\n", m.name, v.size());
        out += line;
    }

    // By family, and by why it was kept.
    auto share = [&](const char *title, std::function<std::string(const Row &)> group) {
        std::map<std::string, std::pair<int, int>> by; // good, all
        for (auto &r : rows)
        {
            auto g = group(r);
            if (g.empty())
                continue;
            by[g].first += r.good;
            by[g].second++;
        }
        if (by.empty())
            return;
        out += std::string("\n") + title + "\n";
        for (auto &[g, n] : by)
            out += "  " + g + ": " + std::to_string(n.first) + " of " + std::to_string(n.second) + " good (" +
                   fmt("%.0f", 100.0 * n.first / n.second) + "%)\n";
    };
    share("Good finds by kind:", [](const Row &r) { return r.c ? std::string(family_name(r.c->family)) : std::string(); });
    share("Good finds by why it was kept:", [](const Row &r) {
        if (!r.c || !r.normal)
            return std::string();
        bool looks = r.c->verdict.looks >= 1 && r.c->score.score >= kMinScore;
        bool acts = r.c->verdict.acts != 0;
        return std::string(looks && acts ? "looks and acts different" : looks ? "looks different only" : "acts different only");
    });
    size_t gone = std::count_if(rows.begin(), rows.end(), [](const Row &r) { return !r.c; });
    if (gone)
        out += "\n" + std::to_string(gone) +
               " rated candidate(s) have left the top since (the explorer was still running): only find and "
               "score count for those.\n";
    return out;
}

} // namespace corrupt
