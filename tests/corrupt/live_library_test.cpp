#include "doctest/doctest.h"

#include "corrupt/library.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace corrupt;
namespace fs = std::filesystem;

namespace
{

struct TempLibrary
{
    fs::path root = fs::temp_directory_path() /
                    ("live-lib-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Library lib{ root };
    GameId game{ "GOOF TROOP", "0123456789abcdef", "" };
    fs::path dir() { return lib.game_dir(game); }
    void write(const fs::path &rel, const std::string &text)
    {
        fs::create_directories((dir() / rel).parent_path());
        std::ofstream(dir() / rel) << text;
    }
    ~TempLibrary() { fs::remove_all(root); }
};

} // namespace

TEST_SUITE("live library")
{
    TEST_CASE("what syncing leaves behind is recognised")
    {
        CHECK(is_sync_leftover("calls.erik.sync-conflict-20261102-201512-ABCDEFG.json"));
        CHECK(is_sync_leftover(".syncthing.calls.erik.json.tmp"));
        CHECK(is_sync_leftover("~syncthing~calls.erik.json.tmp"));
        CHECK(is_sync_leftover(".stfolder"));
        CHECK_FALSE(is_sync_leftover("calls.erik.json"));
        CHECK_FALSE(is_sync_leftover("gravity-moon.json"));
    }

    TEST_CASE("a conflict copy is nobody's file and no recipe; the panel can list them")
    {
        TempLibrary t;
        NameList names{ { 0x9000, "gravity" } };
        REQUIRE(t.lib.save_names(t.game, "erik", names).empty());
        t.write("names.erik.sync-conflict-20261102-201512-ABCDEFG.json", encode_names({ { 0xA000, "jump" } }));
        auto loaded = t.lib.load_names(t.game, "mikael");
        CHECK(loaded.others.size() == 1);
        CHECK(loaded.label(0xA000).empty());

        CallLog log;
        log.note(0x100, callop::Jsr, 0x9000);
        log.end_frame();
        REQUIRE(t.lib.save_call_log(t.game, "erik", log, 0x10000).empty());
        t.write("calls.erik.sync-conflict-20261102-201512-ABCDEFG.json", encode_call_log(log, 0x10000));
        auto calls = t.lib.load_call_logs(t.game, "mikael", 0x10000);
        CHECK(calls.errors.empty());
        CHECK(calls.others.all().at({ 0x100, 0x9000 }).hits == 1); // not counted twice

        Recipe r;
        r.name = "Moon jump";
        r.game = t.game;
        REQUIRE(t.lib.save_recipe(t.game, r).error.empty());
        auto saved = t.dir() / "recipes" / "moon-jump.json";
        REQUIRE(fs::exists(saved));
        fs::copy_file(saved, t.dir() / "recipes" / "moon-jump.sync-conflict-20261102-201512-ABCDEFG.json");
        t.write("recipes/.syncthing.moon-jump.json.tmp", "{");
        auto list = t.lib.list_recipes(t.game);
        REQUIRE(list.size() == 1);
        CHECK(list[0].name == "Moon jump");

        auto conflicts = t.lib.conflict_copies(t.game);
        REQUIRE(conflicts.size() == 3);
        CHECK(conflicts[0] == "calls.erik.sync-conflict-20261102-201512-ABCDEFG.json");
        CHECK(conflicts[2] == "recipes/moon-jump.sync-conflict-20261102-201512-ABCDEFG.json");
    }

    TEST_CASE("the others' stamp changes when someone else's file does, not when yours does")
    {
        TempLibrary t;
        CallLog log;
        log.note(0x100, callop::Jsr, 0x9000);
        log.end_frame();
        REQUIRE(t.lib.save_call_log(t.game, "erik", log, 0x10000).empty());
        auto before = t.lib.others_stamp(t.game, "mikael");
        CHECK_FALSE(before.empty());
        REQUIRE(t.lib.save_call_log(t.game, "mikael", log, 0x10000).empty());
        REQUIRE(t.lib.save_names(t.game, "mikael", { { 0x9000, "gravity" } }).empty());
        CHECK(t.lib.others_stamp(t.game, "mikael") == before);
        log.note(0x200, callop::Jsr, 0xA000);
        REQUIRE(t.lib.save_call_log(t.game, "erik", log, 0x10000).empty()); // bigger now
        CHECK(t.lib.others_stamp(t.game, "mikael") != before);
        before = t.lib.others_stamp(t.game, "mikael");
        REQUIRE(t.lib.save_names(t.game, "erik", { { 0x9000, "falling" } }).empty());
        CHECK(t.lib.others_stamp(t.game, "mikael") != before);
        before = t.lib.others_stamp(t.game, "mikael");
        t.write("calls.erik.sync-conflict-20261102-201512-ABCDEFG.json", "x");
        CHECK(t.lib.others_stamp(t.game, "mikael") == before); // leftovers don't count

        auto recipes = t.lib.recipes_stamp(t.game);
        Recipe r;
        r.name = "Moon jump";
        r.game = t.game;
        REQUIRE(t.lib.save_recipe(t.game, r).error.empty());
        CHECK(t.lib.recipes_stamp(t.game) != recipes);
    }
}
