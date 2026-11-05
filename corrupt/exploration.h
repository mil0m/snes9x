#pragma once
#include "explore.h"
#include "recipe.h"

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// What an exploration (#45) leaves behind, for the app's Explored page (#46) and the gallery:
// one folder per exploration with candidates.json, and for the best variants their recipe, a
// kept moment of their run and a picture strip.
namespace corrupt
{

struct Candidate
{
    uint32_t rank = 0; // 1: the best
    uint32_t id = 0;   // the variant's number in the exploration
    Family family = Family::Ram;
    std::string what;
    Recipe recipe;
    RunScore score;
    std::string moment_file; // in the folder; empty when none was kept
    std::string strip_file;
    std::optional<bool> exact; // its moment played back to exactly its run (checked at the end)
};

struct FamilyCount
{
    uint32_t runs = 0, alive = 0, visible = 0;
};

// One variant as tried (#80): enough to chart what the exploration did.
struct Tried
{
    enum Outcome : uint8_t
    {
        Stopped,   // crashed, froze, or didn't play to the end
        Unchanged, // ran, nothing visibly changed
        Visible,   // ran, and changed something you can see
    };
    Family family = Family::Ram;
    Outcome outcome = Unchanged;
    float score = 0;
};

struct Exploration
{
    std::string name;   // the folder's: "<moment> 5 Nov 21.13"
    GameId game;
    std::string moment; // the kept moment it started from
    std::string made_by, made_at;
    uint32_t window = 0;   // frames each variant played
    uint32_t variants = 0, alive = 0, visible = 0;
    double seconds = 0;    // how long it ran
    bool finished = false; // false: still running (or stopped early); what's there is the best so far
    std::array<FamilyCount, kFamilies> families{};
    std::vector<Candidate> candidates; // best first
    std::vector<Tried> tried;          // every variant, in the order tried (#80; empty in older files)
};

// A survivor whose picture differs this much from the control's (in the window's second half)
// changed something you can see.
constexpr double kVisible = 0.02;
// A candidate scores at least this to be kept. Measured on held-out blind ratings: below 0.05 a
// variant rated 1.3 of 5 on average, 0.05-0.3 1.9, 0.3-1 2.3, from 1 up 3.4; a quiet scene
// keeps a few candidates rather than 24 that look like nothing.
constexpr double kMinScore = 0.3;

// What the charts show (#80), worked out from an exploration.
struct KindStats
{
    Family family = Family::Ram;
    uint32_t runs = 0, stopped = 0, unchanged = 0, visible = 0, kept = 0;
    double visible_share() const { return runs ? double(visible) / runs : 0; }
};
struct ScoreBin
{
    double lo = 0, hi = 0; // [lo, hi); the last bin's hi is infinity
    uint32_t count = 0;
    bool kept_side = false; // at or above the keep line
};
struct FindPoint
{
    uint32_t at = 0; // its number in the run (0-based)
    double score = 0;
    bool kept = false; // among the candidates now
};
struct ExploreStats
{
    std::vector<KindStats> kinds;   // the families tried, the most often visible first
    bool detailed = false;          // has `tried`: the bins and the finds below exist
    std::vector<ScoreBin> bins;     // visible changes' scores
    uint32_t zero_scores = 0;       // tries that ran and changed nothing (not in the bins)
    std::vector<FindPoint> finds;   // tries at or above the keep line, in order
    std::vector<std::pair<uint32_t, double>> best; // the best score so far, where it rose
    uint32_t tried = 0;
};
ExploreStats stats_of(const Exploration &e);

std::string encode_exploration(const Exploration &e); // candidates.json
std::optional<Exploration> decode_exploration(const std::string &json, std::string &error);

// A page that shows it all, self-contained (the strips inline): `read` gives a file of the
// folder's bytes, or nothing.
std::string gallery_html(const Exploration &e, const std::function<std::optional<std::string>(const std::string &)> &read);

} // namespace corrupt
