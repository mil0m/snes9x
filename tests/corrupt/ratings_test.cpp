#include "doctest/doctest.h"
#include "corrupt/library.h"
#include "corrupt/ratings.h"

#include <chrono>
#include <fstream>

using namespace corrupt;
namespace fs = std::filesystem;

namespace
{
Candidate candidate(uint32_t id, double find, double score, Family family = Family::Code)
{
    Candidate c;
    c.id = id;
    c.rank = id;
    c.family = family;
    c.verdict.find = find;
    c.verdict.looks = score / 0.5;
    c.verdict.kept = true;
    c.score.score = score;
    c.score.different = 0.5;
    return c;
}
} // namespace

TEST_SUITE("ratings (#83)")
{
    TEST_CASE("a person's ratings: set, change, take back, and the file keeps them")
    {
        Ratings r;
        auto a = candidate(17, 2.4, 1.2), b = candidate(40, 1.1, 0.4);
        set_rating(r, a, true, "2026-11-07 10:00:00");
        set_rating(r, b, false, "2026-11-07 10:01:00");
        REQUIRE(r.size() == 2);
        REQUIRE(rating_of(r, 17));
        CHECK(rating_of(r, 17)->good);
        CHECK(rating_of(r, 17)->find == doctest::Approx(2.4)); // what it was rated on
        CHECK(rating_of(r, 17)->score == doctest::Approx(1.2));
        set_rating(r, a, false, "2026-11-07 10:02:00"); // changed: still one
        CHECK(r.size() == 2);
        CHECK(!rating_of(r, 17)->good);
        set_rating(r, b, std::nullopt, "");
        CHECK(r.size() == 1);
        CHECK(!rating_of(r, 40));

        std::string error;
        auto back = decode_ratings(encode_ratings(r), error);
        REQUIRE_MESSAGE(back, error);
        REQUIRE(back->size() == 1);
        CHECK((*back)[0].id == 17);
        CHECK(!(*back)[0].good);
        CHECK((*back)[0].at == "2026-11-07 10:02:00");
        CHECK(!decode_ratings("{", error));
        CHECK(!decode_ratings(R"({"version": 9, "ratings": []})", error));
        // A bad entry doesn't lose the rest; a repeated id counts once.
        auto mixed = decode_ratings(
            R"({"version": 1, "ratings": [{"id": "x", "good": true}, {"id": 3, "good": true}, {"id": 3, "good": false}]})", error);
        REQUIRE(mixed);
        REQUIRE(mixed->size() == 1);
        CHECK((*mixed)[0].good);
    }

    TEST_CASE("everyone's ratings in words: you first, then by name")
    {
        PeoplesRatings all;
        auto c = candidate(5, 1.5, 0.8);
        set_rating(all["erbl"], c, false, "t");
        set_rating(all["mikael"], c, true, "t");
        set_rating(all["anna"], c, true, "t");
        CHECK(ratings_text(all, 5, "mikael") == "You: good find. anna: good find. erbl: not a find.");
        CHECK(ratings_text(all, 6, "mikael").empty());
    }

    TEST_CASE("the library keeps one ratings file per person beside the exploration, and lists them with it")
    {
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto root = fs::temp_directory_path() / ("ratings-test-" + std::to_string(stamp));
        Library lib(root);
        Exploration e;
        e.name = "Temple 7 Nov 10.00";
        e.game = { "POCKY ROCKY", "72b2b3be0123", "1234abcd" };
        e.made_at = "2026-11-07 10:00:00";
        e.candidates = { candidate(17, 2.4, 1.2) };
        auto dir = lib.new_exploration_dir(e.game, e.name);
        REQUIRE(dir.error.empty());
        { std::ofstream(dir.path / "candidates.json") << encode_exploration(e); }

        Ratings mine, erik;
        set_rating(mine, e.candidates[0], true, "t");
        set_rating(erik, e.candidates[0], false, "t");
        CHECK(Library::save_ratings(dir.path, "Mikael Z", mine).empty());
        CHECK(Library::save_ratings(dir.path, "erbl", erik).empty());
        CHECK(Library::ratings_path(dir.path, "Mikael Z").filename() == "ratings.mikael-z.json");
        // A sync's conflict copy and a broken file aren't anyone's ratings.
        { std::ofstream(dir.path / "ratings.erbl.sync-conflict-20261107-1234.json") << encode_ratings(mine); }
        { std::ofstream(dir.path / "ratings.broken.json") << "{"; }

        auto list = lib.list_explorations(e.game);
        REQUIRE(list.size() == 1);
        auto &all = list[0].ratings;
        CHECK(all.size() == 2);
        REQUIRE(all.count("mikael-z"));
        REQUIRE(all.count("erbl"));
        CHECK(all.at("mikael-z")[0].good);
        CHECK(!all.at("erbl")[0].good);
        std::vector<std::string> errors;
        Library::load_ratings(dir.path, &errors);
        REQUIRE(errors.size() == 1);
        CHECK(errors[0].find("ratings.broken.json") == 0);
        // explore-ratings reads an explored/ folder without knowing the game.
        CHECK(Library::explorations_in(lib.explored_dir(e.game)).size() == 1);
        fs::remove_all(root);
    }

    TEST_CASE("the report: how many, how often people agree, how well each measure picks the good finds")
    {
        CHECK(ratings_report({}).find("No ratings yet") == 0);

        RatedExploration x;
        x.exploration.normal.runs = 37;
        x.exploration.normal.score = 0.5;
        // Finds 1..6: the higher, the better people liked it, except that score runs the other way.
        for (uint32_t i = 1; i <= 6; i++)
            x.exploration.candidates.push_back(candidate(i, double(i), 2.0 - i * 0.1, i <= 3 ? Family::Ram : Family::Kill));
        x.exploration.candidates[5].measured.new_functions = 4;
        x.exploration.candidates[5].verdict.acts = acts::NewFunctions;
        for (auto &c : x.exploration.candidates)
            set_rating(x.ratings["mikael"], c, c.id >= 4, "t");
        // Erik agrees on two of three.
        set_rating(x.ratings["erbl"], x.exploration.candidates[0], false, "t");
        set_rating(x.ratings["erbl"], x.exploration.candidates[4], true, "t");
        set_rating(x.ratings["erbl"], x.exploration.candidates[5], false, "t");
        // A rating whose candidate left the top: counts for find and score only.
        Candidate gone = candidate(99, 1.2, 0.6);
        set_rating(x.ratings["erbl"], gone, false, "t");

        auto text = ratings_report({ x });
        INFO(text);
        CHECK(text.find("Ratings: 10 in 1 exploration(s), 4 good finds, 6 not.") == 0);
        CHECK(text.find("erbl: 1 good, 3 not") != std::string::npos);
        CHECK(text.find("mikael: 3 good, 3 not") != std::string::npos);
        CHECK(text.find("agreed 2 of 3 times (67%)") != std::string::npos);
        CHECK(text.find("find (the rank)") != std::string::npos);
        CHECK(text.find("kill: 4 of 5 good (80%)") != std::string::npos);
        CHECK(text.find("ram: 0 of 4 good (0%)") != std::string::npos);
        CHECK(text.find("acts different only") == std::string::npos); // each looks different (looks >= 1)
        CHECK(text.find("looks and acts different: 1 of 2 good") != std::string::npos);
        CHECK(text.find("1 rated candidate(s) have left the top") != std::string::npos);
    }
}
