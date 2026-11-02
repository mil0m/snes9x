#pragma once
#include "corrupt/buttons.h"
#include "corrupt/code_map.h"
#include "corrupt/engine.h"
#include "corrupt/library.h"
#include "corrupt/policy.h"
#include "corrupt/recipe_json.h"
#include "corrupt/save_backup.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// What the corruption panel and the Workshop (#63) see of the session, and what they can do
// to it. The app's SessionControl forwards to CorruptionSession; the widget tests use a fake.
struct PanelStatus
{
    std::vector<corrupt::ButtonSlot> buttons; // the recipe's buttons 1-9, named (#63)
    bool game_loaded = false;
    corrupt::GameId game;
    corrupt::EngineStatus engine;
    bool has_recipe = false;
    std::vector<corrupt::Rule> rules;
    std::string notes;
    uint64_t recipe_generation = 0; // bumps whenever a recipe is loaded
    uint64_t rules_generation = 0;  // bumps on every rule edit too
    uint64_t library_generation = 0; // bumps when the game's recipes change on disk (a shared library, #62)
    double intensity = 1.0;          // the recipe's (Chaos)
    bool starting = false;           // a start waits for the next frame (the game is paused)
    bool has_moment = false;         // the recipe has a saved moment on disk
    bool moment_pending = false;     // the next start jumps to that moment
    bool new_moment = false;         // the current run began somewhere Save hasn't written yet
    bool saved_moments = true;
    std::filesystem::path recipe_path; // the file it came from; empty for a new recipe
    bool edited = false;               // differs from that file (rules, seed, name...)
    std::array<size_t, corrupt::kTargetCount> sizes{}; // the loaded game's memory sizes
    std::string message;             // last notable event, for the panel: a few words
    std::string message_detail;      // the same in full (on hover), or empty
    std::string rom_name;            // the game's file name without its extension ("Pocky & Rocky (USA)")
    bool paused = false;             // the emulation is paused (the app's, not the session's)

    // Crash guard.
    corrupt::PolicyKind policy = corrupt::PolicyKind::Reroll;
    bool policy_overridden = false;
    bool detection_enabled = true;
    bool test_first_enabled = true;
    unsigned crashes = 0;
    std::string last_crash;
    std::vector<unsigned> rule_crashes;
    size_t hot_blocks = 0; // crash map: 16-byte blocks rules with avoid_hot stay out of

    bool can_go_back = false; // a run started (its moment is there to go back to), or the ROM changed
    bool back_to_normal = false; // Back to normal was the last thing done (nothing has run since)
    bool normal_here = false;    // ...the kind that stayed where the game was (#70)
    size_t rom_size = 0;
    size_t rom_changed = 0; // ROM bytes that differ from the clean ROM
    size_t rom_used = 0;    // ...of which the game has run or read since they changed
    size_t functions = 0;     // the call graph's functions (#26)
    size_t functions_off = 0; // of them, switched off now (#34)
    size_t functions_guarded = 0; // in play, their bodies guarded (#58)
    size_t guarded_bytes = 0;
    size_t survivors = 0;   // ...of which proved themselves (see survivors())
    unsigned instant_reverts = 0; // changed bytes caught sending the CPU astray, fixed at once (#51)
    unsigned tested_first = 0;    // live ROM firings played ahead before they landed (#52)
    unsigned tested_rerolled = 0; // ...that crashed and rolled again
    unsigned tested_skipped = 0;  // ...whose every pick crashed: skipped
    bool instant_revert = true;   // the recipe lets it
    bool instant_enabled = true;  // and the panel's switch does (#63)
    std::vector<corrupt::KeptItem> kept; // what the recipe keeps besides its rules (#63)
    size_t pins = 0;        // ROM changes the recipe keeps
    size_t patched = 0;     // painted bytes kept in the recipe
    corrupt::CodeCoverage code; // the code map (everyone's), updated every few seconds
    size_t jump_tables = 0;     // jump tables found (everyone's), updated with it
    size_t jump_swappable = 0;  // ...of which have another entry to swap with
};

// Remote control from a browser (#53), as the Sharing page shows it.
struct RemoteState
{
    bool enabled = false;
    std::string status;  // "Off. Nobody can control this from a browser."
    std::string address; // the public address typed in, if any
};

class CorruptionControl
{
  public:
    virtual ~CorruptionControl() = default;
    virtual PanelStatus status() = 0;

    // Playing.
    virtual void setActive(bool on) = 0;
    virtual void backToNormal() = 0;
    virtual void normalHere() = 0; // corruption off and the ROM clean, the game playing on from where it is (#70)
    virtual uint64_t reroll() = 0; // the new seed
    virtual void setSeed(uint64_t seed) = 0;
    virtual void pressButton(int key) = 0;
    virtual void setRuleEnabled(size_t index, bool on) = 0;
    virtual void setRuleIntensity(size_t index, double intensity) = 0;
    virtual void setIntensity(double intensity) = 0;
    virtual void markCrash() = 0;

    // The recipe. Errors come back as text, empty when it worked.
    virtual std::vector<corrupt::RecipeEntry> recipes() = 0; // the game's, from the library
    virtual std::vector<std::string> loadRecipe(const corrupt::Recipe &recipe, const std::filesystem::path &path) = 0;
    virtual void newRecipe(const std::string &name) = 0;
    virtual corrupt::SaveResult save() = 0;
    virtual corrupt::SaveResult saveAs(const std::string &name, const std::string &notes) = 0;
    virtual std::string shareRecipe(const std::filesystem::path &path) = 0;
    virtual std::string importRecipe(const std::filesystem::path &path, std::string &name) = 0;
    virtual void setSavedMoments(bool on) = 0;
    virtual corrupt::Regions regions() = 0;
    virtual std::string addRule(const corrupt::Rule &rule) = 0;
    virtual std::string replaceRule(size_t index, const corrupt::Rule &rule) = 0;
    virtual std::string removeRule(size_t index) = 0;
    virtual std::string removeKept(const corrupt::KeptItem &item) = 0;

    // Safety.
    virtual void setDetectionEnabled(bool on) = 0;
    virtual void setTestFirstEnabled(bool on) = 0;
    virtual void setInstantEnabled(bool on) = 0;
    virtual void setPolicyOverride(std::optional<corrupt::PolicyKind> policy) = 0;
    virtual std::filesystem::path backupDir() = 0;
    virtual std::vector<corrupt::BackupInfo> backups() = 0;
    virtual std::string restoreBackup(const std::filesystem::path &path) = 0; // and restarts the game

    // The ROM.
    virtual void restoreCleanRom() = 0;
    virtual void clearPatches() = 0;
    virtual std::string exportRom(const std::filesystem::path &path) = 0;

    // The library.
    virtual std::filesystem::path libraryRoot() = 0;
    virtual void setLibraryRoot(const std::filesystem::path &root) = 0;
    virtual std::filesystem::path gameFolder() = 0; // empty without a game
    virtual std::vector<corrupt::Library::Player> players() = 0;
    virtual std::vector<std::string> conflicts() = 0;
    virtual std::string me() = 0; // this player's name in the library

    // Remote control.
    virtual RemoteState remote() = 0;
    virtual std::string setRemote(bool on) = 0;
    virtual void setRemoteAddress(const std::string &address) = 0;
    virtual std::string remoteLink() = 0;
    virtual void newRemoteKey() = 0;
};
