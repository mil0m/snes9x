#include "CorruptionSession.hpp"
#include "EmuConfig.hpp"
#include "corrupt/hook.h"
#include "corrupt/journal.h"
#include "corrupt/recipe_json.h"

#include "snes9x.h"
#include "memmap.h"
#include "fscompat.h"
#include "display.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace fs = std::filesystem;
using namespace corrupt;

namespace
{

constexpr uint64_t kBackupCheckFrames = 60; // about once a second

std::string hex_bytes(const unsigned char *data, size_t size)
{
    static const char *digits = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < size; i++)
    {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 0xF];
    }
    return out;
}

GameId current_game()
{
    GameId id;
    id.title = Memory.ROMName;
    id.sha256 = hex_bytes(Memory.ROMSHA256, sizeof(Memory.ROMSHA256));
    char crc[16];
    snprintf(crc, sizeof(crc), "%08x", Memory.ROMCRC32);
    id.crc32 = crc;
    return id;
}

// Whose journal this is: one file per person in a shared library.
std::string user_name()
{
    for (auto var : { "USER", "USERNAME" })
        if (auto v = getenv(var); v && *v)
            return v;
    return "player";
}

uint64_t random_seed()
{
    std::random_device rd;
    return (uint64_t(rd()) << 32) ^ rd();
}

} // namespace

CorruptionSession &CorruptionSession::get()
{
    static CorruptionSession session;
    return session;
}

void CorruptionSession::frameHook()
{
    get().onFrame();
}

void CorruptionSession::configureGuard()
{
    const auto &recipe = engine.recipe();
    guard.set_policy(policy_override.value_or(recipe.crash_policy.value_or(PolicyKind::Reroll)));
    DetectorConfig cfg = recipe.crash_detection.value_or(DetectorConfig{});
    cfg.enabled = cfg.enabled && detection_enabled;
    guard.set_detector_config(cfg);
    guard.on_journal = [this](const JournalEntry &e) { writeJournal(e); };
}

void CorruptionSession::writeJournal(const JournalEntry &entry)
{
    fs::path path = library_root.empty()
                        ? fs::path(EmuConfig::findConfigDir()) / "corruption-journal" /
                              (game_folder_name(game.title, game.sha256) + ".jsonl")
                        : Library(library_root).journal_path(game, user_name());
    auto err = append_journal(path, journal_line(entry, BackupClock::now()));
    if (!err.empty())
        note("Journal: " + err);
}

void CorruptionSession::betweenFrames()
{
    std::lock_guard lock(mutex);
    if (!game_loaded)
        return;
    auto out = guard.between_frames(engine, access);
    if (out.kind == GuardOutcome::Nothing)
        return;
    last_crash = out.summary;
    note(out.summary);
    S9xSetInfoString(out.summary.c_str());
}

void CorruptionSession::markCrash()
{
    std::lock_guard lock(mutex);
    guard.mark_crash();
}

void CorruptionSession::setPolicyOverride(std::optional<PolicyKind> policy)
{
    std::lock_guard lock(mutex);
    policy_override = policy;
    configureGuard();
}

void CorruptionSession::setDetectionEnabled(bool enabled)
{
    std::lock_guard lock(mutex);
    detection_enabled = enabled;
    configureGuard();
}

void CorruptionSession::gameLoaded()
{
    std::lock_guard lock(mutex);
    game_loaded = true;
    game = current_game();
    engine.set_active(false);
    has_recipe = false;
    engine.set_recipe({});
    recipe_generation++;
    rules_generation++;
    recipe_path.clear();
    saved_json.clear();
    guard.reset();
    last_crash.clear();
    configureGuard();
    last_backup_time.reset();
    last_backup_hash.reset();
    frames_since_check = 0;
    S9xCorruptFrameHook = &CorruptionSession::frameHook;

    backupNow("load"); // before the first frame runs
    message = "Loaded " + game_folder_name(game.title, game.sha256);
}

void CorruptionSession::gameClosing()
{
    std::lock_guard lock(mutex);
    if (!game_loaded)
        return;
    backupNow("close");
    engine.set_active(false);
    game_loaded = false;
}

void CorruptionSession::setLibraryRoot(const fs::path &root)
{
    std::lock_guard lock(mutex);
    library_root = root;
}

fs::path CorruptionSession::libraryRoot()
{
    std::lock_guard lock(mutex);
    return library_root;
}

Library CorruptionSession::library()
{
    std::lock_guard lock(mutex);
    return Library(library_root);
}

std::vector<std::string> CorruptionSession::loadRecipe(const Recipe &recipe, const fs::path &path)
{
    std::lock_guard lock(mutex);
    std::vector<std::string> problems;
    if (!game_loaded)
    {
        problems.push_back("load a game first");
        return problems;
    }
    for (size_t i = 0; i < recipe.rules.size(); i++)
        if (auto err = validate_rule(recipe.rules[i], memory))
            problems.push_back("rules[" + std::to_string(i) + "] " + recipe.rules[i].name + ": " + *err);
    if (!problems.empty())
        return problems;

    bool was_active = engine.active();
    engine.set_recipe(recipe);
    engine.set_active(was_active);
    has_recipe = true;
    recipe_generation++;
    rules_generation++;
    recipe_path = path;
    saved_json = path.empty() ? std::string() : recipeJson(recipe);
    guard.reset();
    last_crash.clear();
    configureGuard();
    note("Recipe: " + recipe.name);
    return problems;
}

// What Save writes. A recipe without a game (an example, a new one) gets the loaded game's.
std::string CorruptionSession::recipeJson(Recipe recipe) const
{
    if (recipe.game.sha256.empty())
        recipe.game = game;
    return write_recipe(recipe);
}

void CorruptionSession::newRecipe(const std::string &name)
{
    std::lock_guard lock(mutex);
    if (!game_loaded)
        return;
    Recipe recipe;
    recipe.name = name;
    recipe.game = game;
    recipe.seed = random_seed();
    bool was_active = engine.active();
    engine.set_recipe(recipe);
    engine.set_active(was_active);
    has_recipe = true;
    recipe_generation++;
    rules_generation++;
    recipe_path.clear();
    saved_json.clear();
    guard.reset();
    last_crash.clear();
    configureGuard();
    note("New recipe: " + name);
}

std::string CorruptionSession::replaceRule(size_t index, const Rule &rule)
{
    std::lock_guard lock(mutex);
    if (!has_recipe || index >= engine.recipe().rules.size())
        return "no such rule";
    if (auto err = validate_rule(rule, memory))
        return *err;
    engine.replace_rule(index, rule);
    rules_generation++;
    note("Changed rule " + rule.name);
    return {};
}

std::string CorruptionSession::addRule(const Rule &rule)
{
    std::lock_guard lock(mutex);
    if (!has_recipe)
        return "load or start a recipe first";
    if (auto err = validate_rule(rule, memory))
        return *err;
    engine.add_rule(rule);
    rules_generation++;
    note("Added rule " + rule.name);
    return {};
}

std::string CorruptionSession::removeRule(size_t index)
{
    std::lock_guard lock(mutex);
    if (!has_recipe || index >= engine.recipe().rules.size())
        return "no such rule";
    std::string name = engine.recipe().rules[index].name;
    engine.remove_rule(index);
    rules_generation++;
    note("Removed rule " + name);
    return {};
}

void CorruptionSession::setActive(bool active)
{
    std::lock_guard lock(mutex);
    engine.set_active(active && has_recipe && game_loaded);
}

bool CorruptionSession::toggleActive()
{
    std::lock_guard lock(mutex);
    bool on = !engine.active() && has_recipe && game_loaded;
    engine.set_active(on);
    if (!has_recipe)
        note("No recipe loaded");
    return on;
}

uint64_t CorruptionSession::reroll()
{
    std::lock_guard lock(mutex);
    uint64_t seed = random_seed();
    engine.reroll(seed);
    return seed;
}

void CorruptionSession::setSeed(uint64_t seed)
{
    std::lock_guard lock(mutex);
    engine.reroll(seed);
}

void CorruptionSession::pressHotkey(int n)
{
    std::lock_guard lock(mutex);
    engine.press_hotkey(n);
}

void CorruptionSession::setRuleEnabled(size_t index, bool enabled)
{
    std::lock_guard lock(mutex);
    engine.set_rule_enabled(index, enabled);
}

void CorruptionSession::setRuleIntensity(size_t index, double intensity)
{
    std::lock_guard lock(mutex);
    engine.set_rule_intensity(index, std::clamp(intensity, 0.0, 1.0));
}

void CorruptionSession::setIntensity(double intensity)
{
    std::lock_guard lock(mutex);
    engine.set_intensity(intensity);
}

double CorruptionSession::stepIntensity(double delta)
{
    std::lock_guard lock(mutex);
    // Snap to the 10 % grid, so steps from a slider value like 37 % land on round numbers.
    double next = std::round((engine.recipe().intensity + delta) * 10.0) / 10.0;
    engine.set_intensity(next);
    return engine.recipe().intensity;
}

SaveResult CorruptionSession::saveRecipeAs(const std::string &name, const std::string &notes)
{
    std::lock_guard lock(mutex);
    SaveResult result;
    if (library_root.empty())
    {
        result.error = "choose a library folder first";
        return result;
    }
    if (!has_recipe)
    {
        result.error = "no recipe loaded";
        return result;
    }
    Recipe recipe = engine.recipe();
    recipe.name = name;
    recipe.notes = notes;
    recipe.game = game;
    result = Library(library_root).save_recipe(game, recipe);
    if (result.error.empty())
    {
        engine.set_info(name, notes);
        recipe_path = result.path;
        saved_json = recipeJson(recipe);
        recipe_generation++; // the dock re-reads the name and path
        note("Saved " + result.path.filename().string());
    }
    return result;
}

SaveResult CorruptionSession::save()
{
    std::lock_guard lock(mutex);
    SaveResult result;
    if (!has_recipe)
        result.error = "no recipe loaded";
    else if (recipe_path.empty())
        result.error = "this recipe has no file yet: use Save As";
    if (!result.error.empty())
        return result;

    auto json = recipeJson(engine.recipe());
    result.path = recipe_path;
    result.error = write_file_atomic(recipe_path, json);
    if (result.error.empty())
    {
        saved_json = json;
        note("Saved " + recipe_path.filename().string());
    }
    return result;
}

fs::path CorruptionSession::backupDirLocked() const
{
    // In the library when there is one (git-ignored there), otherwise next to the config.
    if (!library_root.empty())
        return Library(library_root).backup_dir(game);
    return fs::path(EmuConfig::findConfigDir()) / "corruption-save-backups" / game_folder_name(game.title, game.sha256);
}

fs::path CorruptionSession::backupDir()
{
    std::lock_guard lock(mutex);
    return backupDirLocked();
}

std::vector<BackupInfo> CorruptionSession::backups()
{
    std::lock_guard lock(mutex);
    if (!game_loaded)
        return {};
    return list_backups(backupDirLocked());
}

std::string CorruptionSession::restoreBackup(const fs::path &path)
{
    std::lock_guard lock(mutex);
    if (!game_loaded)
        return "no game loaded";
    std::vector<uint8_t> bytes;
    if (!read_backup(path, bytes))
        return "couldn't read " + path.string();
    size_t size = memory.size(Target::SRAM);
    if (bytes.size() != size)
        return "backup is " + std::to_string(bytes.size()) + " bytes but this game's save RAM is " + std::to_string(size);

    backupNow("before-restore"); // so a restore can itself be undone
    engine.set_active(false);
    guard.reset();
    memcpy(Memory.SRAM, bytes.data(), size);
    Memory.SaveSRAM(S9xGetFilename(".srm", SRAM_DIR).c_str()); // the .srm on disk too, right away
    last_backup_hash = sram_hash(bytes.data(), size);
    note("Restored " + path.filename().string());
    return {};
}

CorruptionSession::Status CorruptionSession::status()
{
    std::lock_guard lock(mutex);
    Status s;
    s.game_loaded = game_loaded;
    s.game = game;
    s.engine = engine.status();
    s.has_recipe = has_recipe;
    s.rules = engine.recipe().rules;
    s.notes = engine.recipe().notes;
    s.recipe_generation = recipe_generation;
    s.rules_generation = rules_generation;
    s.intensity = engine.recipe().intensity;
    s.recipe_path = recipe_path;
    s.edited = has_recipe && recipeJson(engine.recipe()) != saved_json;
    if (game_loaded)
        for (auto t : kAllTargets)
            s.sizes[size_t(t)] = memory.size(t);
    s.policy = guard.get_policy();
    s.policy_overridden = policy_override.has_value();
    s.detection_enabled = detection_enabled;
    s.crashes = guard.total_crashes();
    s.last_crash = last_crash;
    s.rule_crashes = guard.rule_crashes(engine);
    s.message = message;
    return s;
}

void CorruptionSession::onFrame()
{
    std::lock_guard lock(mutex);
    engine.on_frame(memory);
    access.note_frame_start();
    if (++frames_since_check >= kBackupCheckFrames)
    {
        frames_since_check = 0;
        maybeBackup();
    }
}

void CorruptionSession::maybeBackup()
{
    size_t size = memory.size(Target::SRAM);
    if (!size)
        return;
    auto now = BackupClock::now();
    uint64_t hash = sram_hash(Memory.SRAM, size);
    if (should_backup(now, last_backup_time, hash, last_backup_hash))
        backupNow("auto");
}

void CorruptionSession::backupNow(const char *reason)
{
    size_t size = memory.size(Target::SRAM);
    if (!game_loaded || !size)
        return;

    auto now = BackupClock::now();
    auto dir = backupDirLocked();
    auto result = write_backup(dir, Memory.SRAM, size, now, reason);
    if (!result.error.empty())
    {
        note("Save backup failed: " + result.error);
        return;
    }
    last_backup_time = now;
    last_backup_hash = sram_hash(Memory.SRAM, size);
    prune_backups(dir, now);
}

void CorruptionSession::note(const std::string &text)
{
    message = text;
}
