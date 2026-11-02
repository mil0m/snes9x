#include "doctest/doctest.h"

#include "corrupt/function_names.h"
#include "corrupt/library.h"

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace corrupt;
namespace fs = std::filesystem;

TEST_SUITE("function names")
{
    TEST_CASE("a names file round-trips, in entry order, one a line")
    {
        NameList names{ { 0xA000, "jump" }, { 0x9000, "gravity" } };
        auto text = encode_names(names);
        CHECK(text.find("0x009000") < text.find("0x00A000"));
        CHECK(text.find("\"gravity\"") != std::string::npos);
        std::string error;
        auto back = decode_names(text, error);
        REQUIRE(back);
        CHECK(*back == names);
        CHECK(decode_names(encode_names({}), error)->empty());
    }

    TEST_CASE("bad entries are dropped, a bad file is an error")
    {
        std::string error;
        auto n = decode_names(R"({"format": "snes9x-names", "version": 1, "functions": [
            {"entry": "0x009000", "name": "  gravity "}, {"entry": "9000", "name": "x"},
            {"entry": "0x1000000", "name": "far"}, {"entry": "0x00A000", "name": "   "}, {"name": "no entry"}, 7]})",
                              error);
        REQUIRE(n);
        CHECK(*n == NameList{ { 0x9000, "gravity" } });
        CHECK_FALSE(decode_names("{}", error));
        CHECK(error == "not a names file");
        CHECK_FALSE(decode_names(R"({"format": "snes9x-names"})", error));
    }

    TEST_CASE("labels: mine, theirs with who, both; the same name once")
    {
        FunctionNames n;
        n.set_mine(0x9000, " gravity ");
        n.others[0x9000] = { { "erik", "gravity" }, { "anna", "fall" } };
        n.others[0xA000] = { { "erik", "jump" } };
        CHECK(n.label(0x9000) == "gravity · fall (anna)");
        CHECK(n.label(0xA000) == "jump (erik)");
        CHECK(n.label(0xB000).empty());
        n.set_mine(0x9000, "");
        CHECK(n.label(0x9000) == "gravity (erik) · fall (anna)");
    }

    TEST_CASE("the library keeps one file per person and merges everyone's")
    {
        auto root = fs::temp_directory_path() /
                    ("names-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Library lib(root);
        GameId game{ "GOOF TROOP", "0123456789abcdef", "" };
        CHECK(lib.save_names(game, "Mikael", { { 0x9000, "gravity" } }).empty());
        CHECK(lib.save_names(game, "erik", { { 0x9000, "falling" }, { 0xA000, "jump" } }).empty());
        { std::ofstream(lib.names_path(game, "broken")) << "nope"; }

        auto n = lib.load_names(game, "Mikael");
        CHECK(n.mine == NameList{ { 0x9000, "gravity" } });
        REQUIRE(n.others.count(0x9000));
        CHECK(n.others.at(0x9000).size() == 1);
        CHECK(n.others.at(0x9000)[0].who == "erik");
        CHECK(n.label(0x9000) == "gravity · falling (erik)");
        CHECK(n.label(0xA000) == "jump (erik)");
        REQUIRE(n.errors.size() == 1);
        CHECK(n.errors[0].find("names.broken.json") != std::string::npos);
        fs::remove_all(root);
    }
}
