#pragma once
#include "corrupt/core_memory.h"
#include "corrupt/engine.h"
#include "corrupt/library.h"
#include "corrupt/save_backup.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// Glue between the corruption engine and the running game: owns the engine,
// installs the per-frame hook, and keeps automatic save-RAM backups.
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
        std::string message;             // last notable event, for the panel
    };

    // Called by Snes9xController around ROM loading (emulation thread suspended).
    void gameLoaded();
    void gameClosing();

    void setLibraryRoot(const std::filesystem::path &root);
    std::filesystem::path libraryRoot();
    corrupt::Library library();

    // Engine control. loadRecipe validates against the loaded game and returns the problems, if any.
    std::vector<std::string> loadRecipe(const corrupt::Recipe &recipe);
    void setActive(bool active);
    bool toggleActive(); // returns the new state
    uint64_t reroll();   // returns the new seed
    void setSeed(uint64_t seed);
    void pressHotkey(int n);
    void setRuleEnabled(size_t index, bool enabled);
    corrupt::SaveResult saveRecipeAs(const std::string &name, const std::string &notes);

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

    std::mutex mutex;
    corrupt::Engine engine;
    corrupt::CoreMemory memory;
    std::filesystem::path library_root;
    bool game_loaded = false;
    bool has_recipe = false;
    uint64_t recipe_generation = 0;
    corrupt::GameId game;
    std::string message;

    uint64_t frames_since_check = 0;
    std::optional<corrupt::BackupClock::time_point> last_backup_time;
    std::optional<uint64_t> last_backup_hash;
};
