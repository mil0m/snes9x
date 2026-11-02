#pragma once
#include "corrupt/core_access.h"
#include "corrupt/core_memory.h"
#include "corrupt/crash_guard.h"
#include "corrupt/crash_map_keeper.h"
#include "corrupt/engine.h"
#include "corrupt/library.h"
#include "corrupt/memory_watch.h"
#include "corrupt/rom_keeper.h"
#include "corrupt/instant_revert.h"
#include "corrupt/test_ahead.h"
#include "corrupt/rom_blame.h"
#include "corrupt/rom_heat.h"
#include "corrupt/survivors.h"
#include "FunctionSource.hpp"
#include "corrupt/share.h"
#include "corrupt/run_control.h"
#include "corrupt/save_backup.h"

#include <filesystem>
#include <array>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// Glue between the corruption engine and the running game: owns the engine,
// installs the per-frame hook, guards against crashes (rolling back between
// frames), and keeps automatic save-RAM backups.
//
// Thread-safety: the frame hook runs on the emulation thread; the panel calls
// in from the UI thread. Every public method takes the mutex, which is
// uncontended on almost every frame.
class CorruptionSession
{
  public:
    static CorruptionSession &get();

    struct Status
    {
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
        std::string message;             // last notable event, for the panel

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
        size_t pins = 0;        // ROM changes the recipe keeps
        size_t patched = 0;     // painted bytes kept in the recipe
        corrupt::CodeCoverage code; // the code map (everyone's), updated every few seconds
        size_t jump_tables = 0;     // jump tables found (everyone's), updated with it
        size_t jump_swappable = 0;  // ...of which have another entry to swap with
    };

    // Called by Snes9xController around ROM loading (emulation thread suspended).
    void gameLoaded();
    void gameClosing();
    // The front end loaded a save state: checkpoints from before belong to another timeline.
    void stateLoaded();

    void setLibraryRoot(const std::filesystem::path &root);
    std::filesystem::path libraryRoot();
    corrupt::Library library();

    // Engine control. loadRecipe validates against the loaded game and returns the problems, if any.
    std::vector<std::string> loadRecipe(const corrupt::Recipe &recipe, const std::filesystem::path &path = {});
    void newRecipe(const std::string &name); // an empty recipe for the loaded game
    void setActive(bool active);
    bool toggleActive(); // returns the new state
    // Back to normal: corruption off, the clean ROM back, and the game back at the moment the
    // run started, so what it did to RAM is gone too. Happens between frames.
    void backToNormal();
    uint64_t reroll();   // returns the new seed
    void setSeed(uint64_t seed);
    void pressHotkey(int n);
    void setRuleEnabled(size_t index, bool enabled);
    void setRuleIntensity(size_t index, double intensity);
    void setIntensity(double intensity);  // the whole recipe
    double stepIntensity(double delta);   // returns the new value
    corrupt::SaveResult saveRecipeAs(const std::string &name, const std::string &notes);
    corrupt::SaveResult save(); // over the file the recipe came from
    // Sharing: one .s9xrecipe file with the recipe, its pins and its start moment. Importing
    // saves it in this game's recipes (under a free name) and loads it. Error or empty.
    std::string shareRecipe(const std::filesystem::path &path);
    std::string importRecipe(const std::filesystem::path &path, std::string &name);

    // Live rule edits; the run goes on. Each returns an error, or empty when applied.
    std::string replaceRule(size_t index, const corrupt::Rule &rule);
    std::string addRule(const corrupt::Rule &rule);
    std::string removeRule(size_t index);

    // Crash guard. betweenFrames() runs after every emulated frame (emulation thread).
    void betweenFrames();
    void markCrash();
    void setPolicyOverride(std::optional<corrupt::PolicyKind> policy); // nullopt: the recipe's choice
    void setDetectionEnabled(bool enabled);
    void setTestFirstEnabled(bool enabled); // #52 for every recipe; a recipe's own "test_first" sets how far

    // Saved moments: Save writes the moment the current run started next to the recipe,
    // and loading a recipe that has one jumps back to it when the run starts.
    void setSavedMoments(bool on);

    // Memory viewer: watch one target between frames while the viewer is open.
    void watchMemory(corrupt::Target target);
    void stopWatching();
    void clearHits();
    corrupt::MemorySnapshot memorySnapshot();
    std::vector<corrupt::CopyRun> findCopies(corrupt::Target of); // copies of `of` in WRAM
    std::vector<corrupt::MapArea> memoryMap(corrupt::Target target); // fixed layout plus what the game uses now
    std::vector<uint8_t> palette();                                  // CGRAM as it is now, for the tile view

    // Named regions of the loaded game (its regions.json). Each returns an error or empty.
    corrupt::Regions regions();
    std::string saveRegion(const std::string &name, const corrupt::AddressRange &range);
    std::string renameRegion(const std::string &from, const std::string &to); // recipes and the running one follow
    std::string deleteRegion(const std::string &name);                        // they keep the range, unnamed
    std::vector<std::string> regionUsers(const std::string &name);

    // The ROM. A run always starts from the clean ROM; these act on the current one.
    void restoreCleanRom();
    std::string exportRom(const std::filesystem::path &path); // error or empty; the checksum is fixed

    // Painting: sets the bytes now and keeps them in the recipe as patches, which apply
    // whenever a run starts (a new recipe is made if none is loaded). Error or empty.
    std::string paint(corrupt::Target t, const std::vector<uint32_t> &offsets, uint8_t value);
    // A window of a memory as it is now (for the remote page, #53); empty if there's none.
    std::vector<uint8_t> readMemory(corrupt::Target t, uint32_t offset, uint32_t length);
    void remoteNote(const std::string &text); // the status line says what a remote did
    std::string setBytes(corrupt::Target t, const std::vector<std::pair<uint32_t, uint8_t>> &bytes); // the same, a value each
    void clearPatches(); // forgets them; the bytes stay until the next run starts

    // Survivors: changed ROM bytes the game used often with no crash after. Pinning keeps a
    // byte at its value now, in the recipe: applied as every run starts, never undone by
    // the crash guard. Error or empty.
    std::vector<corrupt::Survivor> survivors();
    std::vector<corrupt::Pin> pins();
    std::string pin(const std::vector<uint32_t> &offsets, const std::string &note);
    void unpin(const std::vector<uint32_t> &offsets);

    // Kill switches (#34): the functions the call graph knows, everyone's, busiest now
    // first, with whether each is off and the recipe's switch for it. A flip happens at the
    // next frame while corruption is on. Errors or empty.
    std::vector<FunctionRow> functions();
    bool corruptionOn();
    std::string flipFunction(uint32_t entry);
    std::string keepSwitch(uint32_t entry, int hotkey, bool start_off, const std::string &note, uint32_t every = 0,
                           uint32_t for_frames = 0);
    void forgetSwitch(uint32_t entry);
    // Guards and names (#58, #59). Protect guards a function's body (deep: and what it
    // calls) without changing it; setName writes your name for it to the library.
    std::string protectFunction(uint32_t entry, bool on, bool deep);
    std::string setFunctionName(uint32_t entry, const std::string &name);
    // Change a function (#60): see FunctionSource. Applying needs corruption on, like a flip.
    std::vector<corrupt::ChangeOption> changeOptions(uint32_t entry);
    std::vector<uint8_t> romBytes(uint32_t at, size_t n, bool clean);
    std::optional<std::vector<uint8_t>> redirectBytes(const corrupt::ChangeOption &o, uint32_t entry);
    std::string applyChange(const corrupt::Change &c, bool on);
    std::string keepChange(const corrupt::Change &c);
    void forgetChange(uint32_t at);

    // Save backups.
    std::filesystem::path backupDir();
    std::vector<corrupt::BackupInfo> backups();
    std::string restoreBackup(const std::filesystem::path &path); // error or empty; caller resets the game

    Status status();

  private:
    CorruptionSession() = default;
    void newRecipeLocked(const std::string &name);
    static void frameHook();
    void onFrame();
    void backupNow(const char *reason);
    void maybeBackup();
    void note(const std::string &message);
    std::filesystem::path backupDirLocked() const;
    void configureGuard();
    void pinChanged(); // the blame leaves the recipe's pins alone
    std::string recipeJson(corrupt::Recipe recipe) const;
    std::string changeRegion(const std::string &from, const std::string &to);
    std::string writeMoment(corrupt::Recipe &recipe, const std::filesystem::path &recipe_path); // error or empty
    bool newMoment() const;
    void writeJournal(const corrupt::JournalEntry &entry);
    void loadCrashMap();
    void saveCrashMap();
    void loadCodeMap(); // emulation thread or suspended: the tracer's buffer is replaced
    void saveCodeMap();
    void saveJumpLog();
    void saveCallLog();
    void findJumpTables(const corrupt::CodeMap &all); // when the log or the map grew since last time
    void findBodies(const corrupt::CodeMap &all);     // the same for function bodies (#58)
    void loadNames();
    void rebuildGuard(); // the functions in play's bodies to the engine; cleans them; the overlay
    std::string guardedBy(uint32_t offset) const; // "$C0:9000 (gravity)", for messages

    std::mutex mutex;
    corrupt::Engine engine;
    corrupt::CoreMemory memory;
    corrupt::RomKeeper rom{ memory }; // what the engine writes through: journals ROM edits, guards the header
    corrupt::RomBlame rom_blame{ rom }; // which changed ROM bytes the game used, for the guard
    corrupt::SnesCoreAccess access;
    corrupt::CrashGuard guard;
    corrupt::RunControl control;
    corrupt::MemoryWatch watch;
    corrupt::WramAccessCounter wram_access; // counting only while the viewer watches WRAM
    corrupt::CrashMapKeeper crash_map;
    corrupt::CodeMapKeeper code_map;        // recorded into by the tracer whenever a game is loaded
    corrupt::RomTracer tracer;
    corrupt::DerailWatch derails;  // the tracer notes landings on non-code in it (#51)
    corrupt::InstantRevert instant; // and this replays such a frame without the culprits
    bool watching_derails = false;  // the watch costs 5-10% while on: only during ROM runs
    corrupt::TestAhead ahead;       // live ROM changes played ahead, unseen, before they land (#52)
    bool running_ahead = false;     // the frame hook runs inside betweenFrames, which holds the lock
    void testAhead();
    corrupt::RomAddresses rom_where;        // for disassembly in the viewer
    std::vector<uint8_t> code_shown;        // everyone's map, copied between frames while the viewer shows the ROM
    std::vector<uint8_t> code_for_aim;      // everyone's map as aimed rules saw it when this run started
    corrupt::CodeCoverage code_coverage;
    corrupt::RomHeat rom_heat;              // the viewer's Live overlay, while it shows the ROM
    corrupt::JumpLogKeeper jump_log;        // the tracer notes indirect jumps into it, kept like the code map
    corrupt::CallKeeper call_log;           // ...and calls (#26): the functions kill switches turn off
    corrupt::FunctionBodies bodies;         // each function's code (#58), from everyone's map, every few seconds
    uint64_t bodies_print = ~uint64_t(0);
    corrupt::FunctionNames names;           // everyone's names for the functions (#59)
    std::vector<uint32_t> window_off;       // switched off from the Functions window: in play while off
    std::vector<corrupt::Change> window_changes; // applied from Change It…, not kept: in play while on
    bool guard_dirty = false;               // rebuild the guard between frames
    // Playing together live (#62): what everyone else's files and the recipes looked like.
    std::string others_seen, recipes_seen;
    std::vector<std::string> conflicts_seen;
    uint64_t library_generation = 0;
    void checkLibrary(); // every 30 s between frames: reload when someone else's files changed
    std::vector<corrupt::InPlay> in_play;
    std::vector<uint8_t> function_shown;    // the overlay's marks, while the viewer shows the ROM
    std::vector<corrupt::ShownFunction> function_list;
    corrupt::RomOffsets rom_offsets;        // where each 4 KB of the address space is in the ROM
    std::vector<corrupt::JumpTable> jump_tables; // found from everyone's log and map, every few seconds
    std::vector<uint8_t> jump_shown;        // the viewer's marks for them
    uint64_t jumps_found_print = ~uint64_t(0);
    std::filesystem::path code_map_root;    // the library the map came from (and is saved to)
    bool code_map_reload = false;           // the library changed: reload between frames
    bool back_to_normal = false;            // asked for; done between frames
    bool at_normal = false;                 // done, and no run since
    uint64_t code_frames = 0;
    std::vector<corrupt::MapArea> live_map; // VRAM or ARAM layout, re-read between frames while watched
    bool saved_moments = true;
    bool moment_on_disk = false; // the current run's start is the recipe's saved moment
    std::optional<corrupt::PolicyKind> policy_override;
    bool detection_enabled = true;
    bool test_first_enabled = true;
    std::string last_crash;
    std::filesystem::path library_root;
    bool game_loaded = false;
    bool has_recipe = false;
    uint64_t recipe_generation = 0;
    uint64_t rules_generation = 0;
    std::filesystem::path recipe_path;
    std::string saved_json; // the recipe as last loaded or saved, to tell whether it was edited
    corrupt::GameId game;
    std::string message;

    uint64_t frames_since_check = 0;
    std::optional<corrupt::BackupClock::time_point> last_backup_time;
    std::optional<uint64_t> last_backup_hash;
};
