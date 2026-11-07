#pragma once
#include "exploration.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

// People's ratings of an exploration's candidates (#83): after pressing Play, good find or
// not. One file per person beside the exploration (ratings.<user>.json), so two people's
// ratings sync without ever writing the same file, and the explorer, which rewrites
// candidates.json while it runs, never touches them.
namespace corrupt
{

struct Rating
{
    uint32_t id = 0;   // the candidate's variant number: stays when ranks change
    bool good = false; // a good find: the game itself broken or acting wrong, interestingly
    std::string at;    // "2026-11-07 10:00:00"
    // What it was rated on: a candidate can leave the top while the explorer still runs.
    double find = 0, score = 0;
};
using Ratings = std::vector<Rating>; // one person's, for one exploration

std::string encode_ratings(const Ratings &r);
std::optional<Ratings> decode_ratings(const std::string &json, std::string &error);
// Rates a candidate, or takes its rating back (good empty).
void set_rating(Ratings &r, const Candidate &c, std::optional<bool> good, const std::string &at);
const Rating *rating_of(const Ratings &r, uint32_t id);

// Everyone's ratings for one exploration, by who (the part of the file name after "ratings.").
using PeoplesRatings = std::map<std::string, Ratings>;
// "You: good find. erbl: not a find." You first (`me`: your file name part), then by name;
// empty when nobody rated it.
std::string ratings_text(const PeoplesRatings &all, uint32_t id, const std::string &me);

// What explore-ratings prints: for every rated candidate, how well each measure the explorer
// has separates good finds from the rest (AUC), how often two people agree, by family.
struct RatedExploration
{
    Exploration exploration;
    PeoplesRatings ratings;
};
std::string ratings_report(const std::vector<RatedExploration> &list);

} // namespace corrupt
