#pragma once
#include "envelope.h"
#include "explore.h"
#include "recipe.h"

#include <algorithm>
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

// A survivor whose picture differs this much from the control's (in the window's second half)
// changed something you can see.
constexpr double kVisible = 0.02;
// A candidate scores at least this to be kept. Measured on held-out blind ratings: below 0.05 a
// variant rated 1.3 of 5 on average, 0.05-0.3 1.9, 0.3-1 2.3, from 1 up 3.4; a quiet scene
// keeps a few candidates rather than 24 that look like nothing. Since #82 the floor under
// normal play's best.
constexpr double kMinScore = 0.3;

// Normal play's best (#82): the most any bar-setting placebo (the moment with one press
// shifted, dropped or added, no corruption) reached. A variant has to go beyond it.
struct NormalBest
{
    uint32_t runs = 0; // the runs without corruption normal play was measured from first; 0: an older exploration
    double score = 0;
    uint32_t new_functions = 0, silenced = 0, new_code = 0;
    // #84: placebos played between the variants, each judged against the bars so far before it
    // joined them; how many of them would have been kept.
    uint32_t later = 0, later_kept = 0;
    // One bar-setting placebo; one that stopped sets nothing. True when a bar rose.
    bool add(const RunScore &s, const Signals &g);
    double keep_line() const { return std::max(kMinScore, score); }
};

// Ways a run acts different from normal play: code signals past normal play's best.
namespace acts
{
constexpr uint8_t NewFunctions = 1; // calls functions no natural run called
constexpr uint8_t Silenced = 2;     // functions every natural run called often went quiet
constexpr uint8_t NewCode = 4;      // ran more code never run before than any placebo
} // namespace acts

struct Verdict
{
    double looks = 0;  // the score over the keep line (1: as different as normal play gets)
    uint8_t acts = 0;  // acts:: flags, only when it changed something you can see
    double find = 0;   // looks + 1 per act; 1 or more exactly when kept
    bool kept = false; // alive, and looks past normal play's best or acts different
};
// A run against normal play. With no normal play (best.runs 0) the old rule: the score
// against kMinScore.
Verdict judge(const RunScore &s, const Signals &g, const NormalBest &best);
// "3 functions normal play never calls; 2 it always calls went quiet": the acts in words.
std::string acts_text(uint8_t acts, const Signals &g, const RunScore &s);

struct Candidate
{
    uint32_t rank = 0; // 1: the best
    uint32_t id = 0;   // the variant's number in the exploration
    Family family = Family::Ram;
    std::string what;
    Recipe recipe;
    RunScore score;
    Signals measured; // against normal play (#82)
    Verdict verdict;
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
    float find = 0; // the verdict's (#82; 0 in older files)
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
    NormalBest normal; // what normal play reached (#82; runs 0 in older files)
    std::vector<Candidate> candidates; // best first
    std::vector<Tried> tried;          // every variant, in the order tried (#80; empty in older files)
};

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
    uint32_t at = 0;  // its number in the run (0-based)
    double value = 0; // its find strength (by_find) or its score
    bool kept = false; // among the candidates now
};
struct ExploreStats
{
    std::vector<KindStats> kinds;   // the families tried, the most often visible first
    bool detailed = false;          // has `tried`: the bins and the finds below exist
    std::vector<ScoreBin> bins;     // visible changes' scores
    uint32_t zero_scores = 0;       // tries that ran and changed nothing (not in the bins)
    std::vector<FindPoint> finds;   // tries kept-worthy, in order
    std::vector<std::pair<uint32_t, double>> best; // the best value so far, where it rose
    double keep_line = kMinScore;   // the scores' keep line: normal play's best (#82) or the old floor
    bool by_find = false;           // finds are by strength (1: normal play's best), not by score
    uint32_t tried = 0;
};
ExploreStats stats_of(const Exploration &e);

// "Normal play: 37 runs without corruption ...": what the bars are, in words (#82).
std::string normal_text(const NormalBest &n);
// "Looks different beyond normal play: score 1.4, normal play's best 0.62. Acts different: ...".
std::string why_kept(const Candidate &c, const NormalBest &n);

std::string encode_exploration(const Exploration &e); // candidates.json
std::optional<Exploration> decode_exploration(const std::string &json, std::string &error);

// A page that shows it all, self-contained (the strips inline): `read` gives a file of the
// folder's bytes, or nothing.
std::string gallery_html(const Exploration &e, const std::function<std::optional<std::string>(const std::string &)> &read);

} // namespace corrupt
