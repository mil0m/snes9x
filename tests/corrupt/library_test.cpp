#include "doctest/doctest.h"
#include "corrupt/library.h"

#include <algorithm>
#include <chrono>
#include <fstream>

using namespace corrupt;
namespace fs = std::filesystem;

namespace
{
struct TempDir
{
    fs::path path;
    TempDir()
    {
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("corrupt-lib-test-" + std::to_string(stamp));
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
};

const GameId kGame{ "SUPER MARIOWORLD", "a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90", "b19ed489" };

Recipe sample(const std::string &name)
{
    Recipe r;
    r.name = name;
    r.game = kGame;
    r.seed = 5;
    Rule rule;
    rule.name = "noise";
    rule.range = { Target::WRAM, 0x100, 0x1FF };
    rule.op = { .type = OpType::Random, .min = 0, .max = 255 };
    rule.when = { .type = TriggerType::EveryFrames, .every = 60 };
    r.rules.push_back(rule);
    return r;
}

void write_file(const fs::path &p, const std::string &text)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}
} // namespace

TEST_CASE("game folder names")
{
    CHECK(game_folder_name("SUPER MARIOWORLD", kGame.sha256) == "SUPER MARIOWORLD [a1b2c3d4]");
    CHECK(game_folder_name("  ZELDA   ", "ABCDEF0123") == "ZELDA [abcdef01]");    // trimmed, lowercase hash
    CHECK(game_folder_name("A/B:C*D?", "00000000") == "A_B_C_D_ [00000000]");     // filesystem-hostile chars
    CHECK(game_folder_name("\xB6\xB0\xBB\xDE", "11111111") == "____ [11111111]"); // half-width katakana bytes
    CHECK(game_folder_name("", "22222222") == "Unknown game [22222222]");
    CHECK(game_folder_name("A  B", "33333333") == "A B [33333333]");               // runs of spaces collapse
}

TEST_CASE("recipe file names")
{
    CHECK(recipe_file_stem("Purple Link chaos") == "purple-link-chaos");
    CHECK(recipe_file_stem("  Boss HP x2!!  ") == "boss-hp-x2");
    CHECK(recipe_file_stem("???") == "recipe");
}

TEST_CASE("save, list and load round-trip")
{
    TempDir tmp;
    Library lib(tmp.path);

    auto saved = lib.save_recipe(kGame, sample("Purple Link chaos"));
    REQUIRE_MESSAGE(saved.error.empty(), saved.error);
    CHECK(saved.path == tmp.path / "SUPER MARIOWORLD [a1b2c3d4]" / "recipes" / "purple-link-chaos.json");
    CHECK(fs::exists(saved.path));

    REQUIRE(lib.save_recipe(kGame, sample("Another one")).error.empty());

    auto list = lib.list_recipes(kGame);
    REQUIRE(list.size() == 2);
    CHECK(list[0].name == "Another one"); // sorted by name
    CHECK(list[1].name == "Purple Link chaos");
    REQUIRE(list[1].recipe);
    CHECK(list[1].recipe->seed == 5);
    CHECK_FALSE(list[1].sha_mismatch);
}

TEST_CASE("first save creates the game folder with a .gitignore for save backups")
{
    TempDir tmp;
    Library lib(tmp.path);
    REQUIRE(lib.save_recipe(kGame, sample("x")).error.empty());
    std::ifstream ignore(lib.game_dir(kGame) / ".gitignore");
    std::string text((std::istreambuf_iterator<char>(ignore)), {});
    CHECK(text.find("save-backups/") != std::string::npos);
}

TEST_CASE("saving twice under the same name overwrites, leaving no temp files")
{
    TempDir tmp;
    Library lib(tmp.path);
    auto r = sample("Same");
    REQUIRE(lib.save_recipe(kGame, r).error.empty());
    r.seed = 99;
    REQUIRE(lib.save_recipe(kGame, r).error.empty());
    auto list = lib.list_recipes(kGame);
    REQUIRE(list.size() == 1);
    CHECK(list[0].recipe->seed == 99);

    int files = 0;
    for ([[maybe_unused]] auto &e : fs::directory_iterator(lib.game_dir(kGame) / "recipes"))
        files++;
    CHECK(files == 1);
}

TEST_CASE("a recipe for a different ROM version is flagged, not rejected")
{
    TempDir tmp;
    Library lib(tmp.path);
    auto r = sample("Other version");
    r.game.sha256 = "ffff";
    REQUIRE(lib.save_recipe(kGame, r).error.empty());
    auto list = lib.list_recipes(kGame);
    REQUIRE(list.size() == 1);
    CHECK(list[0].recipe);
    CHECK(list[0].sha_mismatch);
}

TEST_CASE("a broken recipe file is listed as broken without hiding the others")
{
    TempDir tmp;
    Library lib(tmp.path);
    REQUIRE(lib.save_recipe(kGame, sample("Good")).error.empty());
    write_file(lib.game_dir(kGame) / "recipes" / "broken.json", "{ nope");
    write_file(lib.game_dir(kGame) / "recipes" / "notes.txt", "ignored");

    auto list = lib.list_recipes(kGame);
    REQUIRE(list.size() == 2);
    auto broken = std::find_if(list.begin(), list.end(), [](auto &e) { return !e.recipe; });
    REQUIRE(broken != list.end());
    CHECK(broken->name == "broken"); // falls back to the file name
    CHECK_FALSE(broken->errors.empty());
}

TEST_CASE("recipes resolve the game's named regions")
{
    TempDir tmp;
    Library lib(tmp.path);
    Regions regions;
    regions["hp"] = { Target::WRAM, 0x0F00, 0x0F01 };
    REQUIRE(lib.save_regions(kGame, regions).empty());

    write_file(lib.game_dir(kGame) / "recipes" / "uses-region.json",
               R"({"format":1,"name":"r","rules":[{"target":"wram","range":"hp","op":{"type":"random"},"when":{"hotkey":1}}]})");

    auto list = lib.list_recipes(kGame);
    REQUIRE(list.size() == 1);
    REQUIRE_MESSAGE(list[0].recipe, (list[0].errors.empty() ? "" : list[0].errors[0]));
    CHECK(list[0].recipe->rules[0].range.first == 0x0F00);

    std::vector<std::string> errors;
    CHECK(lib.load_regions(kGame, errors).size() == 1);
    CHECK(errors.empty());
}

TEST_CASE("an unknown game has no recipes and no regions")
{
    TempDir tmp;
    Library lib(tmp.path);
    std::vector<std::string> errors;
    CHECK(lib.list_recipes(kGame).empty());
    CHECK(lib.load_regions(kGame, errors).empty());
    CHECK(errors.empty());
}
