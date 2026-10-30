#include "doctest/doctest.h"
#include "corrupt/library.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

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

TEST_CASE("the backup folder is git-ignored even before any recipe is saved")
{
    TempDir tmp;
    Library lib(tmp.path);
    auto dir = lib.backup_dir(kGame);
    CHECK(dir == lib.game_dir(kGame) / "save-backups");
    CHECK(fs::is_directory(dir));
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

TEST_SUITE("saved moments")
{
    TEST_CASE("a recipe's moment lives next to it")
    {
        CHECK(anchor_path("/lib/G [1]/recipes/purple-link.json") == fs::path("/lib/G [1]/recipes/purple-link.anchor.frz"));
        CHECK(anchor_path("x.json").filename() == "x.anchor.frz");
    }

    TEST_CASE("moments round-trip as binary, and don't show up as recipes")
    {
        TempDir tmp;
        Library lib(tmp.path);
        auto saved = lib.save_recipe(kGame, sample("with moment"));
        REQUIRE(saved.error.empty());

        std::vector<uint8_t> state;
        for (int i = 0; i < 70000; i++)
            state.push_back(uint8_t(i * 7)); // includes zero bytes and every value
        auto path = anchor_path(saved.path);
        CHECK(save_anchor(path, state).empty());

        std::string error;
        auto back = load_anchor(path, error);
        REQUIRE(back);
        CHECK(*back == state);
        CHECK(error.empty());
        CHECK(lib.list_recipes(kGame).size() == 1);
    }

    TEST_CASE("a missing moment is an error that names the file")
    {
        std::string error;
        CHECK_FALSE(load_anchor("/nowhere/gone.anchor.frz", error));
        CHECK(error.find("gone.anchor.frz") != std::string::npos);
    }
}

TEST_SUITE("named regions")
{
    std::string read(const fs::path &p)
    {
        std::ifstream in(p);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    // A library with regions "stats" and "enemies"; recipe A uses "stats", B uses nothing.
    struct RegionLib
    {
        TempDir tmp;
        Library lib{ tmp.path };
        fs::path a, b;

        RegionLib()
        {
            REQUIRE(lib.set_region(kGame, "stats", { Target::WRAM, 0x0F00, 0x0F3F }).empty());
            REQUIRE(lib.set_region(kGame, "enemies", { Target::WRAM, 0x1200, 0x12FF }).empty());
            write_file(lib.game_dir(kGame) / "recipes" / "a.json",
                       R"({"format":1,"name":"a","rules":[{"name":"x","target":"wram","range":"stats","op":{"type":"random"},"when":{"hotkey":1}},)"
                       R"({"name":"y","target":"wram","range":["0x10","0x20"],"op":{"type":"random"},"when":{"hotkey":2}}]})");
            b = lib.save_recipe(kGame, sample("b")).path;
            a = lib.game_dir(kGame) / "recipes" / "a.json";
        }

        Regions regions()
        {
            std::vector<std::string> errors;
            auto r = lib.load_regions(kGame, errors);
            CHECK(errors.empty());
            return r;
        }

        const Recipe &recipe(const std::string &name)
        {
            static std::vector<RecipeEntry> keep;
            keep = lib.list_recipes(kGame);
            for (auto &e : keep)
                if (e.name == name)
                {
                    REQUIRE_MESSAGE(e.recipe, (e.errors.empty() ? "" : e.errors[0]));
                    return *e.recipe;
                }
            FAIL("no recipe " << name);
            return *keep[0].recipe;
        }
    };

    TEST_CASE("region names")
    {
        CHECK_FALSE(validate_region_name("player stats"));
        CHECK_FALSE(validate_region_name("enemy_table-2"));
        CHECK(validate_region_name(""));
        CHECK(validate_region_name("2nd player"));
        CHECK(validate_region_name("hp!"));
        CHECK(validate_region_name(std::string(41, 'a')));
    }

    TEST_CASE("set adds and redefines; recipes pick the new range up")
    {
        RegionLib r;
        CHECK(r.regions().size() == 2);
        CHECK(r.recipe("a").rules[0].range.first == 0x0F00u);
        CHECK(r.lib.set_region(kGame, "stats", { Target::WRAM, 0x0E00, 0x0E1F }).empty());
        CHECK(r.regions().at("stats").first == 0x0E00u);
        CHECK(r.recipe("a").rules[0].range.first == 0x0E00u);
        CHECK_FALSE(r.lib.set_region(kGame, "bad!", { Target::WRAM, 0, 1 }).empty());
        CHECK_FALSE(r.lib.set_region(kGame, "huge", { Target::OAM, 0, 0x300 }).empty()); // past the end
    }

    TEST_CASE("which recipes use a region")
    {
        RegionLib r;
        CHECK(r.lib.recipes_using_region(kGame, "stats") == std::vector<std::string>{ "a" });
        CHECK(r.lib.recipes_using_region(kGame, "enemies").empty());
    }

    TEST_CASE("rename rewrites the recipes that use it, and only those")
    {
        RegionLib r;
        auto b_before = read(r.b);
        auto change = r.lib.rename_region(kGame, "stats", "player stats");
        CHECK(change.error.empty());
        CHECK(change.recipes == std::vector<fs::path>{ r.a });
        CHECK(r.regions().count("player stats") == 1);
        CHECK(r.regions().count("stats") == 0);
        CHECK(r.recipe("a").rules[0].region == "player stats");
        CHECK(r.recipe("a").rules[0].range.first == 0x0F00u);
        CHECK(r.recipe("a").rules[1].region.empty());
        CHECK(read(r.b) == b_before);
    }

    TEST_CASE("rename refuses a taken, missing or bad name")
    {
        RegionLib r;
        CHECK_FALSE(r.lib.rename_region(kGame, "stats", "enemies").error.empty());
        CHECK_FALSE(r.lib.rename_region(kGame, "nope", "fine").error.empty());
        CHECK_FALSE(r.lib.rename_region(kGame, "stats", "").error.empty());
        CHECK(r.regions().count("stats") == 1);
    }

    TEST_CASE("delete writes the range into the recipes that used it")
    {
        RegionLib r;
        auto change = r.lib.delete_region(kGame, "stats");
        CHECK(change.error.empty());
        CHECK(change.recipes == std::vector<fs::path>{ r.a });
        CHECK(r.regions().count("stats") == 0);
        auto &rule = r.recipe("a").rules[0];
        CHECK(rule.region.empty());
        CHECK(rule.range.first == 0x0F00u);
        CHECK(rule.range.last == 0x0F3Fu);
        CHECK_FALSE(r.lib.delete_region(kGame, "stats").error.empty()); // already gone
    }

    TEST_CASE("notes in regions.json survive every edit")
    {
        RegionLib r;
        write_file(r.lib.game_dir(kGame) / "regions.json",
                   R"({"format":1,"regions":{"stats":{"target":"wram","range":["0x0F00","0x0F3F"],"notes":"HP, MP, level"},)"
                   R"("enemies":{"target":"wram","range":["0x1200","0x12FF"],"notes":"8 slots"}}})");
        REQUIRE(r.lib.set_region(kGame, "gold", { Target::WRAM, 0x1600, 0x1602 }).empty());
        REQUIRE(r.lib.rename_region(kGame, "stats", "player").error.empty());
        REQUIRE(r.lib.delete_region(kGame, "gold").error.empty());
        auto text = read(r.lib.game_dir(kGame) / "regions.json");
        CHECK(text.find("HP, MP, level") != std::string::npos);
        CHECK(text.find("8 slots") != std::string::npos);
        auto parsed = parse_regions(text);
        CHECK(parsed.notes.at("player") == "HP, MP, level");
    }
}
