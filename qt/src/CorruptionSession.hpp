#pragma once
#include "corrupt/core_access.h"
#include "corrupt/core_memory.h"
#include "corrupt/crash_guard.h"
#include "corrupt/engine.h"
#include "corrupt/library.h"
#include "corrupt/memory_watch.h"
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

    // Save backups.
    std::filesystem::path backupDir();
    std::vector<corrupt::BackupInfo> backups();
    std::string restoreBackup(const std::filesystem::path &path); // error or empty; caller resets the game

    Status status();

  private:
    CorruptionSession() = default;
    static void frameHook();
    void onFrame();
    void backupNow(const char *reason);
    void maybeBackup();
    void note(const std::string &message);
    std::filesystem::path backupDirLocked() const;
    void configureGuard();
    std::string recipeJson(corrupt::Recipe recipe) const;
    std::string writeMoment(corrupt::Recipe &recipe, const std::filesystem::path &recipe_path); // error or empty
    bool newMoment() const;
    void writeJournal(const corrupt::JournalEntry &entry);

    std::mutex mutex;
    corrupt::Engine engine;
    corrupt::CoreMemory memory;
    corrupt::SnesCoreAccess access;
    corrupt::CrashGuard guard;
    corrupt::RunControl control;
    corrupt::MemoryWatch watch;
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
