#pragma once
#include "corrupt/core_access.h"
#include "corrupt/core_memory.h"
#include "corrupt/crash_guard.h"
#include "corrupt/crash_map_keeper.h"
#include "corrupt/engine.h"
#include "corrupt/library.h"
#include "corrupt/memory_watch.h"
#include "corrupt/rom_keeper.h"
#include "corrupt/rom_blame.h"
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
        unsigned crashes = 0;
        std::string last_crash;
        std::vector<unsigned> rule_crashes;
        size_t hot_blocks = 0; // crash map: 16-byte blocks rules with avoid_hot stay out of

        size_t rom_changed = 0; // ROM bytes that differ from the clean ROM
        size_t rom_used = 0;    // ...of which the game has run or read since they changed
        size_t patched = 0;     // painted bytes kept in the recipe
        corrupt::CodeCoverage code; // the code map (everyone's), updated every few seconds
    };

    // Called by Snes9xController around ROM loading (emulation thread suspended).
    void gameLoaded();
    void gameClosing();

    void setLibraryRoot(const std::filesystem::path &root);
    std::filesystem::path libraryRoot();
    corrupt::Library library();

    // Engine control. loadRecipe validates against the loaded game and returns the problems, if any.
    std::vector<std::string> loadRecipe(const corrupt::Recipe &recipe, const std::filesystem::path &path = {});
    void newRecipe(const std::string &name); // an empty recipe for the loaded game
    void setActive(bool active);
    bool toggleActive(); // returns the new state
    uint64_t reroll();   // returns the new seed
    void setSeed(uint64_t seed);
    void pressHotkey(int n);
    void setRuleEnabled(size_t index, bool enabled);
    void setRuleIntensity(size_t index, double intensity);
    void setIntensity(double intensity);  // the whole recipe
    double stepIntensity(double delta);   // returns the new value
    corrupt::SaveResult saveRecipeAs(const std::string &name, const std::string &notes);
    corrupt::SaveResult save(); // over the file the recipe came from

    // Live rule edits; the run goes on. Each returns an error, or empty when applied.
    std::string replaceRule(size_t index, const corrupt::Rule &rule);
    std::string addRule(const corrupt::Rule &rule);
    std::string removeRule(size_t index);

    // Crash guard. betweenFrames() runs after every emulated frame (emulation thread).
    void betweenFrames();
    void markCrash();
    void setPolicyOverride(std::optional<corrupt::PolicyKind> policy); // nullopt: the recipe's choice
    void setDetectionEnabled(bool enabled);

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
    std::string setBytes(corrupt::Target t, const std::vector<std::pair<uint32_t, uint8_t>> &bytes); // the same, a value each
    void clearPatches(); // forgets them; the bytes stay until the next run starts

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
    std::string recipeJson(corrupt::Recipe recipe) const;
    std::string changeRegion(const std::string &from, const std::string &to);
    std::string writeMoment(corrupt::Recipe &recipe, const std::filesystem::path &recipe_path); // error or empty
    bool newMoment() const;
    void writeJournal(const corrupt::JournalEntry &entry);
    void loadCrashMap();
    void saveCrashMap();
    void loadCodeMap(); // emulation thread or suspended: the tracer's buffer is replaced
    void saveCodeMap();

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
    corrupt::RomAddresses rom_where;        // for disassembly in the viewer
    std::vector<uint8_t> code_shown;        // everyone's map, copied between frames while the viewer shows the ROM
    std::vector<uint8_t> code_for_aim;      // everyone's map as aimed rules saw it when this run started
    corrupt::CodeCoverage code_coverage;
    std::filesystem::path code_map_root;    // the library the map came from (and is saved to)
    bool code_map_reload = false;           // the library changed: reload between frames
    uint64_t code_frames = 0;
    std::vector<corrupt::MapArea> live_map; // VRAM or ARAM layout, re-read between frames while watched
    bool saved_moments = true;
    bool moment_on_disk = false; // the current run's start is the recipe's saved moment
    std::optional<corrupt::PolicyKind> policy_override;
    bool detection_enabled = true;
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
