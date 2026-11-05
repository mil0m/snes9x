#pragma once
#include "code_map.h"
#include "calls.h"
#include "function_names.h"
#include "jump_tables.h"
#include "moment.h"
#include "crash_map.h"
#include "exploration.h"
#include "recipe.h"
#include "recipe_json.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace corrupt
{

// "SUPER MARIOWORLD [a1b2c3d4]": the ROM title made filesystem-safe, plus the
// first 8 hex digits of its SHA-256 so different versions never share a folder.
std::string game_folder_name(const std::string &title, const std::string &sha256_hex);

// "Purple Link chaos" -> "purple-link-chaos"
std::string recipe_file_stem(const std::string &recipe_name);

// What a sync program (Syncthing) leaves in a shared library (#62): conflict copies
// ("x.sync-conflict-<date>-<id>.json"), its temporary files and its folder marker. The
// library never reads them as anyone's file or recipe.
bool is_sync_leftover(const std::string &file_name);

struct RecipeEntry
{
    std::filesystem::path path;
    std::string name;             // the recipe's name, or the file name if it can't be read
    std::optional<Recipe> recipe; // empty when the file is broken
    std::vector<std::string> errors;
    bool sha_mismatch = false;    // made for another version of the ROM; addresses may differ
};

// What a region rename or delete changed: the recipe files it rewrote.
struct RegionChange
{
    std::string error; // empty on success
    std::vector<std::filesystem::path> recipes;
};

// A game's crash maps: this person's (the only one they write) and everyone else's, summed.
struct CrashMaps
{
    CrashMap mine;
    CrashMap others;
    std::vector<std::string> errors; // files that couldn't be read
};

struct SaveResult
{
    std::filesystem::path path;
    std::string error; // empty on success
};

// The recipe library: one folder per game under any root, typically a clone of
// the shared library repo.
class Library
{
  public:
    explicit Library(std::filesystem::path root) : root(std::move(root)) {}

    std::filesystem::path game_dir(const GameId &game) const;

    std::vector<RecipeEntry> list_recipes(const GameId &game) const; // sorted by name
    SaveResult save_recipe(const GameId &game, const Recipe &recipe) const;

    // Where this game's save backups go. Creates it and makes sure the game folder's
    // .gitignore keeps it out of the shared library.
    std::filesystem::path backup_dir(const GameId &game) const;

    // This person's crash journal for the game: journal.<user>.jsonl.
    std::filesystem::path journal_path(const GameId &game, const std::string &user) const;

    // Crash maps: crashmap.<user>.json, one per person, like the journal.
    std::filesystem::path crash_map_path(const GameId &game, const std::string &user) const;
    CrashMaps load_crash_maps(const GameId &game, const std::string &user) const;
    std::string save_crash_map(const GameId &game, const std::string &user, const CrashMap &map) const; // error or empty

    // Code maps: codemap.<user>.bin, one per person. Files for another ROM size are skipped
    // (with an error); the maps come back that size, empty where nobody has played.
    std::filesystem::path code_map_path(const GameId &game, const std::string &user) const;
    CodeMaps load_code_maps(const GameId &game, const std::string &user, size_t rom_size) const;
    std::string save_code_map(const GameId &game, const std::string &user, const CodeMap &map) const; // error or empty
    // Jump logs (jump_tables.h): jumps.<user>.json beside the code maps, one per person.
    std::filesystem::path jump_log_path(const GameId &game, const std::string &user) const;
    JumpLogs load_jump_logs(const GameId &game, const std::string &user, size_t rom_size) const;
    std::string save_jump_log(const GameId &game, const std::string &user, const JumpLog &log, size_t rom_size) const;
    // Call logs (calls.h, #26): calls.<user>.json beside them, one per person.
    std::filesystem::path call_log_path(const GameId &game, const std::string &user) const;
    CallLogs load_call_logs(const GameId &game, const std::string &user, size_t rom_size) const;
    std::string save_call_log(const GameId &game, const std::string &user, const CallLog &log, size_t rom_size) const;

    // Shared names (#59): names.<user>.json, everyone's loaded, only the user's written.
    std::filesystem::path names_path(const GameId &game, const std::string &user) const;
    FunctionNames load_names(const GameId &game, const std::string &user) const;
    std::string save_names(const GameId &game, const std::string &user, const NameList &names) const;

    // Playing together live (#62). Conflict copies a sync left in the game's folder and its
    // recipes (relative paths, sorted), for the panel to mention.
    std::vector<std::string> conflict_copies(const GameId &game) const;
    // A fingerprint of everyone else's learned files for the game (name, size, time): it
    // changes when someone else's file does. Empty when there are none.
    std::string others_stamp(const GameId &game, const std::string &user) const;
    // The same for the game's recipes, for the panel's list.
    std::string recipes_stamp(const GameId &game) const;
    // Everyone whose learned files are in the game's folder (crash maps, code maps, jumps,
    // calls, names, journals), with when their newest file changed; by name. Sync
    // leftovers don't count (#63's Sharing page).
    struct Player
    {
        std::string who;
        std::filesystem::file_time_type newest{};
        size_t files = 0;
    };
    std::vector<Player> players(const GameId &game) const;

    // Kept moments (#47): moments/<name>.s9xmoment in the game's folder, everyone's, newest
    // first. Saving picks a free file name from the moment's name; renaming rewrites the name
    // inside and moves the file to match. Errors or empty.
    std::filesystem::path moments_dir(const GameId &game) const;
    std::vector<MomentInfo> list_moments(const GameId &game, std::vector<std::string> *errors = nullptr) const;
    SaveResult save_moment(const GameId &game, const Moment &moment) const;
    SaveResult rename_moment(const std::filesystem::path &path, const std::string &name) const;
    std::string delete_moment(const std::filesystem::path &path) const;

    // Explorations (#45): explored/<name>/candidates.json in the game's folder, with the best
    // variants' recipes, moments and strips beside it. Everyone's, newest first.
    struct ExplorationEntry
    {
        std::filesystem::path dir;
        Exploration exploration;
    };
    std::filesystem::path explored_dir(const GameId &game) const;
    std::vector<ExplorationEntry> list_explorations(const GameId &game, std::vector<std::string> *errors = nullptr) const;
    // A new folder for an exploration called `name` (a free one: "name 2" if it's taken). Error or empty.
    SaveResult new_exploration_dir(const GameId &game, const std::string &name) const;
    std::string delete_exploration(const std::filesystem::path &dir) const;

    Regions load_regions(const GameId &game, std::vector<std::string> &errors) const;
    std::string save_regions(const GameId &game, const Regions &regions) const; // error or empty

    // Named regions, edited in place (notes in regions.json are kept).
    std::string set_region(const GameId &game, const std::string &name, const AddressRange &range) const; // add or redefine
    RegionChange rename_region(const GameId &game, const std::string &from, const std::string &to) const; // recipes follow
    RegionChange delete_region(const GameId &game, const std::string &name) const; // recipes keep the range, unnamed
    std::vector<std::string> recipes_using_region(const GameId &game, const std::string &name) const;

  private:
    std::string ensure_game_dir(const GameId &game) const;
    RegionsResult read_regions(const GameId &game) const;
    std::string write_regions_file(const GameId &game, const RegionsResult &regions) const;
    RegionChange rewrite_region(const GameId &game, const std::string &from, const std::string &to) const;

    std::filesystem::path root;
};

// A recipe's saved moment (the save state its run started from) sits next to it:
// recipes/purple-link.json -> recipes/purple-link.anchor.frz.
std::filesystem::path anchor_path(const std::filesystem::path &recipe_path);
std::string save_anchor(const std::filesystem::path &path, const std::vector<uint8_t> &state); // error or empty
std::optional<std::vector<uint8_t>> load_anchor(const std::filesystem::path &path, std::string &error);

// Writes to a temporary file, then renames it into place.
std::string write_file_atomic(const std::filesystem::path &path, const std::string &contents);

} // namespace corrupt
