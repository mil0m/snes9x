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
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>

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
    auto &s = get();
    if (s.running_ahead)
    {
        // A frame of a run ahead (#52): betweenFrames already holds the lock, and only the
        // engine belongs in it (no backups of a future that may not happen).
        s.engine.on_frame(s.rom);
        s.access.note_frame_start();
        return;
    }
    s.onFrame();
}

void CorruptionSession::configureGuard()
{
    const auto &recipe = engine.recipe();
    guard.set_policy(policy_override.value_or(recipe.crash_policy.value_or(PolicyKind::Reroll)));
    DetectorConfig cfg = recipe.crash_detection.value_or(DetectorConfig{});
    cfg.enabled = cfg.enabled && detection_enabled;
    guard.set_detector_config(cfg);
    guard.on_journal = [this](const JournalEntry &e) { writeJournal(e); };
    guard.set_byte_blame(&rom_blame);
    rom_blame.set_policy(recipe.rom_policy.value_or(RomPolicy::Revert), recipe.seed);
    instant.set_enabled(recipe.instant_revert.value_or(true));
    AheadConfig ahead_config;
    ahead_config.frames = test_first_enabled ? uint32_t(std::lround(recipe.test_first.value_or(3.0) * 60)) : 0u;
    ahead.set_config(ahead_config);
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
    if (back_to_normal)
    {
        back_to_normal = false;
        engine.set_active(false);
        rom.restore_clean();
        bool moment = control.has_anchor() && access.load_state(control.anchor());
        guard.reset();
        instant.drop();
        at_normal = true;
        note(moment ? "Back to normal: the clean game, at the moment the run started"
                    : "Back to normal: the clean ROM (no run to go back to)");
        S9xSetInfoString("Corruption: back to normal");
    }
    // The instant revert first, before anything reads the frame that ran: a changed ROM byte
    // that sent the CPU into non-code is fixed and the frame played again (#51). The note is
    // taken every frame, which also lets the tracer mark the map again.
    auto caught = instant.after_frame(tracer.take_derail(), engine.current_frame(), access, rom_blame);
    if (caught)
    {
        auto entry = journal_entry(*caught);
        entry.recipe = engine.recipe().name;
        entry.seed = engine.recipe().seed;
        entry.policy = rom_policy_name(rom_blame.policy());
        writeJournal(entry);
        if (crash_map.rom_crashed(caught->offsets))
            engine.set_hot_mask(crash_map.hot_mask());
        note(caught->summary);
        std::string shown = "Corruption: caught a bad jump, " + caught->rom_action;
        S9xSetInfoString(shown.c_str());
    }
    auto started = control.between_frames(engine, access, &rom);
    if (started != RunControl::Started::Nothing)
    {
        at_normal = false;
        moment_on_disk = started == RunControl::Started::FromAnchor;
        pinChanged();
        if (auto skipped = control.last_pins().mismatched)
            note(std::to_string(skipped) + (skipped == 1 ? " pin was" : " pins were") +
                 " made on another version of this ROM and left out");
    }
    switch (started)
    {
    case RunControl::Started::FromAnchor:
        guard.reset(); // its checkpoints belong to the timeline we just left
        instant.drop();
        note("Started from the recipe's saved moment");
        S9xSetInfoString("Corruption: back to the saved moment");
        break;
    case RunControl::Started::AnchorFailed:
        note("The saved moment didn't load; started from here instead");
        break;
    default:
        break;
    }
    // Which changed ROM bytes the game used this frame, for the guard to blame.
    rom_blame.collect(engine.current_frame());
    // The crash map collects this frame's writes before the guard can roll them back.
    crash_map.observe(engine.event_log(), engine.current_frame());
    auto out = guard.between_frames(engine, access);
    rom.after_guard(out); // save states don't hold the ROM: undo its edits after the checkpoint
    // The instant revert watches while a run could change the ROM (the watch costs a little on
    // every instruction), and keeps the state the next frame starts from meanwhile.
    bool rom_run = engine.active() && (rom.changed() > 0 || std::any_of(engine.recipe().rules.begin(),
                                                                        engine.recipe().rules.end(), [](const Rule &r) {
                                                                            return r.enabled && r.range.target == Target::ROM;
                                                                        }));
    bool watch_now = (instant.is_enabled() || ahead.config().frames) && rom_run && tracer.tracing();
    if (watch_now != watching_derails)
    {
        watching_derails = watch_now;
        tracer.watch_derails(watch_now ? &derails : nullptr);
    }
    instant.keep(access, engine.current_frame(), watch_now && instant.is_enabled());
    if (auto survivors = rom_blame.take_survivals(); !survivors.empty())
        crash_map.rom_survived(survivors);
    if (crash_map.crashed(out))
        engine.set_hot_mask(crash_map.hot_mask());
    if (crash_map.save_due())
        saveCrashMap();
    watch.observe(memory, engine.event_log(), engine.current_frame());
    if (watch.watching() && watch.target() == Target::WRAM)
    {
        wram_access.start();
        wram_access.take(watch);
    }
    else
        wram_access.stop();
    // The Live overlay: what the game used this frame. After the blame has collected, since
    // both read the touched bits; the blame passes on the changed bytes it cleared.
    if (watch.watching() && watch.target() == Target::ROM)
        rom_heat.observe(code_map.mine().data(), code_map.mine().size(), rom_blame.used_this_frame());
    else
        rom_heat.pause();
    if (code_map_reload)
    {
        if (code_map.unsaved())
            saveCodeMap(); // to the library it came from
        if (jump_log.unsaved())
            saveJumpLog();
        if (call_log.unsaved())
            saveCallLog();
        loadCodeMap();
    }
    code_map.tick();
    if (code_map.save_due())
        saveCodeMap();
    jump_log.tick();
    if (jump_log.save_due())
        saveJumpLog();
    call_log.end_frame(); // the frame's calls count towards each function's frames (#26)
    if (call_log.save_due())
        saveCallLog();
    if (++code_frames % 30 == 0) // twice a second: copying a 6 MB map isn't free
    {
        if (watch.watching() && watch.target() == Target::ROM)
        {
            code_shown = code_map.combined().flags();
            for (auto &f : code_shown)
                f &= codeflag::All; // the viewer tells known bytes by any flag at all
        }
        else
            code_shown = {};
    }
    if (code_frames % 300 == 1)
    {
        auto all = code_map.combined();
        code_coverage = all.coverage();
        findJumpTables(all);
        findBodies(all);
        guard_dirty = true; // rollbacks may have undone flips; cheap enough every 5 s
    }
    if (guard_dirty)
        rebuildGuard();
    if (watch.watching())
        live_map = watch.target() == Target::VRAM   ? video_map(SnesCoreAccess::video_layout())
                   : watch.target() == Target::ARAM ? sound_map(SnesCoreAccess::sound_layout())
                                                    : std::vector<MapArea>{};
    if (out.kind != GuardOutcome::Nothing)
    {
        last_crash = out.summary;
        note(out.summary);
        S9xSetInfoString(out.summary.c_str());
    }
    testAhead(); // last: it plays the next frame's ROM changes ahead from exactly here
}

void CorruptionSession::testAhead()
{
    if (!watching_derails || !tracer.tracing())
        return; // the watch is on exactly while a run could change the ROM
    AheadHooks hooks;
    hooks.take_derail = [this] { return tracer.take_derail(); };
    hooks.begin = [this] { tracer.note_jumps(false); };
    hooks.end = [this] { tracer.note_jumps(true); };
    running_ahead = true;
    auto tested = ahead.before_frame(engine, rom, access, { code_map.mine().data(), code_map.mine().size() }, hooks);
    running_ahead = false;
    if (tested.summary.empty())
        return;
    note(tested.summary);
    std::string shown = tested.kind == AheadResult::Skipped ? "Corruption: tested ahead, a change skipped"
                                                            : "Corruption: tested ahead, a change re-rolled";
    S9xSetInfoString(shown.c_str());
}

void CorruptionSession::stateLoaded()
{
    std::lock_guard lock(mutex);
    guard.reset();
    instant.drop();
    call_log.forget_now(); // what ran a moment ago is from another moment of the game
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

void CorruptionSession::setTestFirstEnabled(bool enabled)
{
    std::lock_guard lock(mutex);
    test_first_enabled = enabled;
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
    engine.hold_starts(true);
    engine.set_active(false);
    control = RunControl{};
    moment_on_disk = false;
    has_recipe = false;
    engine.set_recipe({});
    recipe_generation++;
    rules_generation++;
    recipe_path.clear();
    saved_json.clear();
    window_off.clear();
    guard_dirty = true;
    guard.reset();
    instant.drop();
    instant.reset_counts();
    ahead.reset_counts();
    last_crash.clear();
    configureGuard();
    last_backup_time.reset();
    last_backup_hash.reset();
    frames_since_check = 0;
    S9xCorruptFrameHook = &CorruptionSession::frameHook;
    SnesCoreAccess::watch_resets(); // for this ROM's reset code
    rom.set_clock([this] { return engine.current_frame(); });
    rom.game_loaded(SnesCoreAccess::locked_rom()); // still clean: nothing has run yet
    engine.set_locked(rom.locked_mask());
    // Aimed ROM rules pick from everyone's code map, as it is when their run starts.
    engine.set_code_flags([this] {
        code_for_aim = code_map.combined().flags();
        for (auto &f : code_for_aim)
            f &= codeflag::All;
        return std::span<const uint8_t>(code_for_aim);
    });
    // Pointer swaps pick from the jump tables found so far (everyone's), as a run starts.
    engine.set_jump_tables([this] { return jump_tables; });
    engine.set_functions([this] { return call_log.functions(); }); // what kill ops pick from (#34)

    backupNow("load"); // before the first frame runs
    message = "Loaded " + game_folder_name(game.title, game.sha256);
    loadCrashMap();
    rom_where = SnesCoreAccess::rom_addresses();
    rom_offsets = SnesCoreAccess::rom_offsets();
    loadCodeMap(); // after the reset watch: the tracer goes on top of it
}

void CorruptionSession::loadCodeMap()
{
    rom_blame.attach(nullptr, 0); // the map it reads is about to be replaced
    tracer.stop();
    code_map_reload = false;
    code_map_root = library_root;
    size_t size = memory.size(Target::ROM);
    CodeMaps maps;
    if (!library_root.empty())
        maps = Library(library_root).load_code_maps(game, user_name(), size);
    for (auto &e : maps.errors)
        note("Code map skipped: " + e);
    code_map.reset(size, std::move(maps));
    rom_heat.reset(size);
    JumpLogs jumps;
    if (!library_root.empty())
        jumps = Library(library_root).load_jump_logs(game, user_name(), size);
    for (auto &e : jumps.errors)
        note("Jump log skipped: " + e);
    jump_log.reset(std::move(jumps));
    CallLogs calls;
    if (!library_root.empty())
        calls = Library(library_root).load_call_logs(game, user_name(), size);
    for (auto &e : calls.errors)
        note("Call log skipped: " + e);
    call_log.reset(std::move(calls));
    auto all = code_map.combined();
    code_coverage = all.coverage();
    code_shown = {};
    jumps_found_print = ~uint64_t(0);
    findJumpTables(all);
    bodies_print = ~uint64_t(0);
    findBodies(all);
    loadNames();
    if (!tracer.start(code_map.mine(), &jump_log.mine(), &call_log.mine()))
        note("The code map isn't recording (no ROM?)");
    else
    {
        rom_blame.attach(code_map.mine().data(), code_map.mine().size());
        if (watching_derails)
            tracer.watch_derails(&derails); // a new tracer: watch again
    }
    // With uses to go by, ROM crashes are learned from them, not from the writes.
    crash_map.learn_rom_by_use(rom_blame.active());
}

void CorruptionSession::saveCodeMap()
{
    if (code_map_root.empty())
        return; // nowhere shared to keep it; it still shows in the viewer
    if (auto err = Library(code_map_root).save_code_map(game, user_name(), code_map.mine()); !err.empty())
        note("Couldn't save the code map: " + err);
    else
        code_map.saved();
}

void CorruptionSession::saveJumpLog()
{
    if (code_map_root.empty())
        return;
    if (auto err = Library(code_map_root).save_jump_log(game, user_name(), jump_log.mine(), memory.size(Target::ROM));
        !err.empty())
        note("Couldn't save the jump log: " + err);
    else
        jump_log.saved();
}

void CorruptionSession::saveCallLog()
{
    if (code_map_root.empty())
        return;
    if (auto err = Library(code_map_root).save_call_log(game, user_name(), call_log.mine(), memory.size(Target::ROM));
        !err.empty())
        note("Couldn't save the call log: " + err);
    else
        call_log.saved();
}

void CorruptionSession::findJumpTables(const CodeMap &all)
{
    auto log = jump_log.combined();
    uint64_t print = log.fingerprint() * 0x9E3779B97F4A7C15ULL ^ all.fingerprint();
    if (print == jumps_found_print)
        return;
    jumps_found_print = print;
    jump_tables = find_jump_tables(log, rom.clean(), all.flags(), rom_offsets).tables;
    jump_shown = jump_marks(jump_tables, all.size());
}

void CorruptionSession::loadCrashMap()
{
    CrashMaps maps;
    if (!library_root.empty())
        maps = Library(library_root).load_crash_maps(game, user_name());
    for (auto &e : maps.errors)
        note("Crash map skipped: " + e);
    crash_map.reset(std::move(maps));
    engine.set_hot_mask(crash_map.hot_mask());
}

void CorruptionSession::saveCrashMap()
{
    if (library_root.empty())
        return; // nowhere shared to keep it; it still steers this session
    if (auto err = Library(library_root).save_crash_map(game, user_name(), crash_map.mine()); !err.empty())
        note("Couldn't save the crash map: " + err);
    if (crash_map.saved())
        engine.set_hot_mask(crash_map.hot_mask());
}

void CorruptionSession::gameClosing()
{
    std::lock_guard lock(mutex);
    if (!game_loaded)
        return;
    backupNow("close");
    if (crash_map.unsaved())
        saveCrashMap();
    engine.set_active(false);
    wram_access.stop();
    if (code_map.unsaved())
        saveCodeMap();
    if (jump_log.unsaved())
        saveJumpLog();
    if (call_log.unsaved())
        saveCallLog();
    rom_blame.attach(nullptr, 0);
    tracer.stop(); // before the reset watch goes
    watching_derails = false;
    instant.drop();
    rom.game_closed();
    SnesCoreAccess::unwatch_resets();
    game_loaded = false;
}

void CorruptionSession::setLibraryRoot(const fs::path &root)
{
    std::lock_guard lock(mutex);
    if (game_loaded && crash_map.unsaved())
        saveCrashMap(); // to the old library, before switching
    library_root = root;
    if (game_loaded)
    {
        loadCrashMap();
        code_map_reload = true; // the tracer writes into the map while frames run
    }
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
    instant.drop();
    instant.reset_counts();
    ahead.reset_counts();
    last_crash.clear();
    configureGuard();
    window_off.clear();
    guard_dirty = true;
    note("Recipe: " + recipe.name);

    // Its saved moment: jump there and start, if the switch is on and it's this ROM.
    control.forget_anchor();
    moment_on_disk = false;
    if (saved_moments && !recipe.anchor_state.empty() && !path.empty())
    {
        std::string error;
        if (!recipe.game.sha256.empty() && recipe.game.sha256 != game.sha256)
            note("Recipe: " + recipe.name + " (its saved moment is from another ROM version, so it starts here)");
        else if (auto state = load_anchor(path.parent_path() / recipe.anchor_state, error))
        {
            control.request_start_from(std::move(*state));
            engine.set_active(true);
            note("Recipe: " + recipe.name + ", starting from its saved moment");
        }
        else
            note("Recipe: " + recipe.name + " (" + error + ", so it starts here)");
    }
    return problems;
}

void CorruptionSession::watchMemory(Target target)
{
    std::lock_guard lock(mutex);
    if (!watch.watching() || watch.target() != target)
        watch.watch(target, game_loaded ? memory.size(target) : 0);
}

void CorruptionSession::stopWatching()
{
    std::lock_guard lock(mutex);
    watch.stop();
}

void CorruptionSession::clearHits()
{
    std::lock_guard lock(mutex);
    watch.clear_hits();
}

MemorySnapshot CorruptionSession::memorySnapshot()
{
    std::lock_guard lock(mutex);
    auto snap = watch.snapshot();
    snap.danger = crash_map.danger(snap.target, snap.bytes.size());
    snap.crash_blocks = crash_map.counts_per_block(snap.target, snap.bytes.size());
    snap.hot_blocks = crash_map.hot_per_block(snap.target, snap.bytes.size());
    if (snap.target == Target::ROM)
    {
        snap.code = code_shown;
        snap.rom_where = rom_where;
        snap.jump_tables = jump_tables;
        snap.jumps = jump_shown;
        snap.live = rom_heat.levels();
        snap.functions = function_shown;
        snap.function_list = function_list;
        if (game_loaded)
            for (uint32_t o : rom_blame.changed_offsets())
            {
                auto *use = rom_blame.use(o);
                snap.rom_changes.push_back({ o, rom.clean()[o], use ? use->frames_used : 0 });
            }
        std::sort(snap.rom_changes.begin(), snap.rom_changes.end(),
                  [](const RomChange &a, const RomChange &b) { return a.offset < b.offset; });
    }
    return snap;
}

namespace
{
std::vector<uint8_t> read_all(MemoryAccess &memory, Target t)
{
    std::vector<uint8_t> bytes(memory.size(t));
    for (uint32_t i = 0; i < bytes.size(); i++)
        bytes[i] = memory.read(t, i);
    return bytes;
}
} // namespace

std::vector<CopyRun> CorruptionSession::findCopies(Target of)
{
    std::lock_guard lock(mutex);
    if (!game_loaded)
        return {};
    return find_copies(read_all(memory, Target::WRAM), read_all(memory, of));
}

std::vector<uint8_t> CorruptionSession::palette()
{
    std::lock_guard lock(mutex);
    if (!game_loaded)
        return {};
    return read_all(memory, Target::CGRAM);
}

std::vector<MapArea> CorruptionSession::memoryMap(Target target)
{
    std::lock_guard lock(mutex);
    if (!game_loaded)
        return {};
    auto map = fixed_map(target, memory.size(target));
    std::vector<MapArea> live; // right after a switch it can still hold the last target's areas
    for (auto &area : live_map)
        if (area.range.target == target)
            live.push_back(area);
    return merge_maps(std::move(map), live);
}

Regions CorruptionSession::regions()
{
    std::lock_guard lock(mutex);
    if (!game_loaded)
        return {};
    std::vector<std::string> errors;
    return Library(library_root).load_regions(game, errors);
}

std::string CorruptionSession::saveRegion(const std::string &name, const AddressRange &range)
{
    std::lock_guard lock(mutex);
    if (!game_loaded || library_root.empty())
        return "load a game and choose a library folder first";
    auto err = Library(library_root).set_region(game, name, range);
    if (err.empty())
        note("Named " + name + ". Recipes using it get its range when they're loaded.");
    return err;
}

std::string CorruptionSession::renameRegion(const std::string &from, const std::string &to)
{
    std::lock_guard lock(mutex);
    return changeRegion(from, to);
}

std::string CorruptionSession::deleteRegion(const std::string &name)
{
    std::lock_guard lock(mutex);
    return changeRegion(name, {});
}

std::vector<std::string> CorruptionSession::regionUsers(const std::string &name)
{
    std::lock_guard lock(mutex);
    if (!game_loaded || library_root.empty())
        return {};
    return Library(library_root).recipes_using_region(game, name);
}

// Rename (or, with an empty `to`, delete) a region in the library, and let the running
// recipe follow. If its file was rewritten, the file is what Save compares against now.
std::string CorruptionSession::changeRegion(const std::string &from, const std::string &to)
{
    if (!game_loaded || library_root.empty())
        return "load a game and choose a library folder first";
    Library library(library_root);
    auto change = to.empty() ? library.delete_region(game, from) : library.rename_region(game, from, to);
    if (!change.error.empty())
        return change.error;
    engine.rename_region(from, to);
    rules_generation++;
    if (std::find(change.recipes.begin(), change.recipes.end(), recipe_path) != change.recipes.end())
    {
        std::ifstream in(recipe_path, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        std::vector<std::string> region_errors;
        if (auto parsed = parse_recipe(text.str(), library.load_regions(game, region_errors)); parsed.recipe)
            saved_json = recipeJson(*parsed.recipe);
    }
    note((to.empty() ? "Deleted region " + from : "Renamed " + from + " to " + to) + ", updating " +
         std::to_string(change.recipes.size()) + " recipe(s)");
    return {};
}

void CorruptionSession::setSavedMoments(bool on)
{
    std::lock_guard lock(mutex);
    saved_moments = on;
}

// The current run began from a state that isn't the recipe's moment on disk.
bool CorruptionSession::newMoment() const
{
    return saved_moments && control.has_anchor() && !moment_on_disk && !engine.waiting();
}

std::string CorruptionSession::writeMoment(Recipe &recipe, const fs::path &path)
{
    if (!newMoment())
        return {};
    auto moment = anchor_path(path);
    if (auto err = save_anchor(moment, control.anchor()); !err.empty())
        return err;
    recipe.anchor_state = moment.filename().string();
    engine.set_anchor_state(recipe.anchor_state);
    moment_on_disk = true;
    return {};
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
    newRecipeLocked(name);
}

void CorruptionSession::newRecipeLocked(const std::string &name)
{
    if (!game_loaded)
        return;
    Recipe recipe;
    recipe.name = name;
    recipe.game = game;
    recipe.seed = random_seed();
    bool was_active = engine.active();
    engine.set_recipe(recipe);
    engine.set_active(was_active);
    control.forget_anchor();
    moment_on_disk = false;
    has_recipe = true;
    recipe_generation++;
    rules_generation++;
    recipe_path.clear();
    saved_json.clear();
    window_off.clear();
    guard_dirty = true;
    guard.reset();
    instant.drop();
    instant.reset_counts();
    ahead.reset_counts();
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

std::string CorruptionSession::shareRecipe(const fs::path &path)
{
    std::string text;
    {
        std::lock_guard lock(mutex);
        if (!has_recipe)
            return "no recipe loaded";
        // The moment this run started from (the saved one, if it started there).
        std::optional<std::vector<uint8_t>> moment;
        if (control.has_anchor())
            moment = control.anchor();
        else if (!engine.recipe().anchor_state.empty() && !recipe_path.empty())
        {
            std::string error;
            moment = load_anchor(recipe_path.parent_path() / engine.recipe().anchor_state, error);
        }
        Recipe recipe = engine.recipe();
        recipe.game = game;
        text = write_shared(recipe, moment ? &*moment : nullptr);
    }
    std::ofstream out(path, std::ios::binary);
    out << text;
    return out ? std::string() : "couldn't write " + path.string();
}

std::string CorruptionSession::importRecipe(const fs::path &path, std::string &name)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return "couldn't read " + path.string();
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string error;
    auto shared = read_shared(text, error);
    if (!shared)
        return error;
    fs::path saved;
    {
        std::lock_guard lock(mutex);
        if (!game_loaded)
            return "load a game first";
        if (library_root.empty())
            return "choose a library folder first";
        Library library(library_root);
        Recipe recipe = shared->recipe;
        // A free name, so an import never replaces a recipe of yours.
        auto dir = library.game_dir(game) / "recipes";
        std::string base = recipe.name.empty() ? "Shared" : recipe.name;
        recipe.name = base;
        for (int n = 2; fs::exists(dir / (recipe_file_stem(recipe.name) + ".json")); n++)
            recipe.name = base + " " + std::to_string(n);
        if (shared->moment)
        {
            auto moment = anchor_path(dir / (recipe_file_stem(recipe.name) + ".json"));
            fs::create_directories(dir);
            if (save_anchor(moment, *shared->moment).empty())
                recipe.anchor_state = moment.filename().string();
        }
        auto result = library.save_recipe(game, recipe);
        if (!result.error.empty())
            return result.error;
        saved = result.path;
        name = recipe.name;
        shared->recipe = recipe;
    }
    auto problems = loadRecipe(shared->recipe, saved);
    return problems.empty() ? std::string() : "imported, but it doesn't fit this game: " + problems[0];
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
    Library library(library_root);
    auto planned = library.game_dir(game) / "recipes" / (recipe_file_stem(name) + ".json");
    if (newMoment())
    {
        if (auto err = writeMoment(recipe, planned); !err.empty())
            note("Saved without its moment: " + err);
    }
    else if (!recipe.anchor_state.empty() && planned != recipe_path && !recipe_path.empty())
    {
        // The run started from the old file's moment: give the copy its own.
        auto copy = anchor_path(planned);
        std::error_code ec;
        std::filesystem::copy_file(recipe_path.parent_path() / recipe.anchor_state, copy,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        recipe.anchor_state = ec ? std::string() : copy.filename().string();
        engine.set_anchor_state(recipe.anchor_state);
    }
    result = library.save_recipe(game, recipe);
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

    Recipe recipe = engine.recipe();
    if (auto err = writeMoment(recipe, recipe_path); !err.empty())
        note("Saved without its moment: " + err);
    auto json = recipeJson(recipe);
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
    instant.drop();
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
    s.starting = engine.waiting();
    s.has_moment = !engine.recipe().anchor_state.empty();
    s.moment_pending = control.start_from_pending();
    s.new_moment = has_recipe && newMoment();
    s.saved_moments = saved_moments;
    s.recipe_path = recipe_path;
    s.edited = has_recipe && recipeJson(engine.recipe()) != saved_json;
    if (game_loaded)
    {
        for (auto t : kAllTargets)
            s.sizes[size_t(t)] = memory.size(t);
        s.rom_changed = rom.changed();
        s.can_go_back = control.has_anchor() || rom.changed() > 0;
        s.back_to_normal = at_normal && !engine.active();
        s.rom_used = rom_blame.used();
        s.rom_size = memory.size(Target::ROM);
        s.survivors = rom_blame.survivor_count(kSurvivorFrames);
        for (auto &f : call_log.functions())
        {
            s.functions++;
            uint8_t ret = return_byte(f.type);
            s.functions_off += ret && f.entry < memory.size(Target::ROM) && memory.read(Target::ROM, f.entry) == ret;
        }
        s.functions_guarded = in_play.size();
        s.guarded_bytes = engine.guarded_mask().count(Target::ROM);
        s.instant_reverts = instant.total();
        s.tested_first = ahead.tested();
        s.tested_rerolled = ahead.rerolled();
        s.tested_skipped = ahead.skipped();
        s.instant_revert = instant.is_enabled();
        s.pins = engine.recipe().pins.size();
        s.patched = patched_bytes(engine.recipe().patches);
    }
    s.policy = guard.get_policy();
    s.policy_overridden = policy_override.has_value();
    s.detection_enabled = detection_enabled;
    s.test_first_enabled = test_first_enabled;
    s.crashes = guard.total_crashes();
    s.last_crash = last_crash;
    s.rule_crashes = guard.rule_crashes(engine);
    s.hot_blocks = crash_map.hot_blocks();
    s.code = code_coverage;
    s.jump_tables = jump_tables.size();
    for (auto &t : jump_tables)
        s.jump_swappable += t.swappable();
    s.message = message;
    return s;
}

void CorruptionSession::backToNormal()
{
    std::lock_guard lock(mutex);
    if (game_loaded)
        back_to_normal = true;
}

void CorruptionSession::restoreCleanRom()
{
    std::lock_guard lock(mutex);
    if (!game_loaded)
        return;
    rom.restore_clean();
    note("The ROM is clean again");
}

std::vector<uint8_t> CorruptionSession::readMemory(Target t, uint32_t offset, uint32_t length)
{
    std::lock_guard lock(mutex);
    if (!game_loaded)
        return {};
    size_t size = memory.size(t);
    const uint8_t *data = memory.data(t);
    if (!data || offset >= size)
        return {};
    size_t n = std::min<size_t>(length, size - offset);
    return { data + offset, data + offset + n };
}

void CorruptionSession::remoteNote(const std::string &text)
{
    std::lock_guard lock(mutex);
    note(text);
}

std::string CorruptionSession::paint(Target t, const std::vector<uint32_t> &offsets, uint8_t value)
{
    std::vector<std::pair<uint32_t, uint8_t>> bytes;
    for (auto o : offsets)
        bytes.push_back({ o, value });
    return setBytes(t, bytes);
}

std::string CorruptionSession::setBytes(Target t, const std::vector<std::pair<uint32_t, uint8_t>> &wanted)
{
    std::lock_guard lock(mutex);
    if (!game_loaded)
        return "No game loaded";
    if (!has_recipe)
        newRecipeLocked("Painted");
    std::vector<std::pair<uint32_t, uint8_t>> bytes;
    size_t size = rom.size(t);
    size_t kept = 0;
    std::optional<uint32_t> first_kept;
    for (auto [o, v] : wanted)
    {
        if (o >= size || rom.locked_mask().is_hot(t, o))
            continue;
        if (engine.guarded_mask().is_hot(t, o))
        {
            kept++;
            if (!first_kept)
                first_kept = o;
            continue;
        }
        bytes.push_back({ o, v });
    }
    if (kept)
        note("Kept " + std::to_string(kept) + (kept == 1 ? " byte" : " bytes") + " as they were: guarded by " +
             guardedBy(*first_kept));
    if (bytes.empty())
        return {};
    engine.add_patches(t, bytes);
    for (auto &[o, v] : bytes)
        rom.write_at_start(t, o, v); // a rollback mustn't undo paint
    return {};
}

void CorruptionSession::pinChanged()
{
    std::vector<uint32_t> offsets;
    for (auto &p : engine.recipe().pins)
        offsets.push_back(p.offset);
    rom_blame.set_pinned(offsets);
}

std::vector<FunctionRow> CorruptionSession::functions()
{
    std::lock_guard lock(mutex);
    std::vector<FunctionRow> rows;
    if (!game_loaded)
        return rows;
    size_t size = memory.size(Target::ROM);
    for (auto &f : call_log.functions())
    {
        FunctionRow r;
        r.f = f;
        r.address = rom_where.cpu_address(f.entry);
        uint8_t ret = return_byte(f.type);
        r.off = ret && f.entry < size && memory.read(Target::ROM, f.entry) == ret;
        for (auto &s : engine.recipe().switches)
            if (s.entry == f.entry)
                r.kept = s;
        for (auto &p : engine.recipe().protects)
            if (p.entry == f.entry)
                r.protect = p;
        r.label = names.label(f.entry);
        if (auto n = names.mine.find(f.entry); n != names.mine.end())
            r.my_name = n->second;
        if (auto b = bodies.find(f.entry); b != bodies.end())
        {
            r.body_bytes = b->second.bytes;
            r.open_ended = b->second.open_ended;
            r.runs = b->second.runs();
        }
        r.guarded = engine.guarded_mask().is_hot(Target::ROM, f.entry);
        rows.push_back(std::move(r));
    }
    return rows;
}

bool CorruptionSession::corruptionOn()
{
    std::lock_guard lock(mutex);
    return game_loaded && engine.active();
}

std::string CorruptionSession::flipFunction(uint32_t entry)
{
    std::lock_guard lock(mutex);
    if (!game_loaded || !engine.active())
        return "corruption is off";
    for (auto &f : call_log.functions())
        if (f.entry == entry)
        {
            uint8_t ret = return_byte(f.type);
            if (!ret)
                return "it's called both near and long";
            if (entry >= rom.clean().size() || rom.locked_mask().is_hot(Target::ROM, entry))
                return "that byte can't be changed";
            engine.flip_function(entry, ret, rom.clean()[entry]);
            if (std::find(window_off.begin(), window_off.end(), entry) == window_off.end())
                window_off.push_back(entry); // in play while it's off (#58)
            guard_dirty = true;
            return {};
        }
    return "no such function";
}

std::string CorruptionSession::keepSwitch(uint32_t entry, int hotkey, bool start_off, const std::string &note_text)
{
    std::lock_guard lock(mutex);
    if (!game_loaded)
        return "No game loaded";
    for (auto &f : call_log.functions())
        if (f.entry == entry)
        {
            uint8_t ret = return_byte(f.type);
            if (!ret)
                return "it's called both near and long";
            if (entry >= rom.clean().size())
                return "that byte isn't in the ROM";
            if (!has_recipe)
                newRecipeLocked("Switches");
            Switch s;
            s.entry = entry;
            s.ret = ret;
            s.was = rom.clean()[entry];
            s.hotkey = std::clamp(hotkey, 0, 9);
            s.start_off = start_off;
            s.note = note_text;
            engine.add_switch(s);
            guard_dirty = true;
            return {};
        }
    return "no such function";
}

void CorruptionSession::forgetSwitch(uint32_t entry)
{
    std::lock_guard lock(mutex);
    engine.remove_switch(entry);
    guard_dirty = true;
}

std::string CorruptionSession::protectFunction(uint32_t entry, bool on, bool deep)
{
    std::lock_guard lock(mutex);
    if (!game_loaded)
        return "No game loaded";
    if (!on)
    {
        engine.remove_protect(entry);
        guard_dirty = true;
        return {};
    }
    if (!bodies.count(entry) || bodies.at(entry).code.empty())
        return "its code isn't known yet: play until it runs";
    if (!has_recipe)
        newRecipeLocked("Protected");
    engine.add_protect({ entry, deep });
    guard_dirty = true;
    return {};
}

std::string CorruptionSession::setFunctionName(uint32_t entry, const std::string &name)
{
    std::lock_guard lock(mutex);
    if (!game_loaded)
        return "No game loaded";
    names.set_mine(entry, name);
    guard_dirty = true; // the overlay's hover text shows names
    if (library_root.empty())
        return "Choose a library folder (More) to keep names and share them";
    return Library(library_root).save_names(game, user_name(), names.mine);
}

void CorruptionSession::loadNames()
{
    names = {};
    if (library_root.empty())
        return;
    names = Library(library_root).load_names(game, user_name());
    for (auto &e : names.errors)
        note("Names skipped: " + e);
}

void CorruptionSession::findBodies(const CodeMap &all)
{
    auto calls = call_log.combined();
    uint64_t print = calls.fingerprint() * 0x9E3779B97F4A7C15ULL ^ all.fingerprint();
    if (print == bodies_print)
        return;
    bodies_print = print;
    std::vector<Function> known;
    for (auto &[key, seen] : calls.all())
        known.push_back({ key.target });
    std::sort(known.begin(), known.end(), [](const Function &a, const Function &b) { return a.entry < b.entry; });
    known.erase(std::unique(known.begin(), known.end(),
                            [](const Function &a, const Function &b) { return a.entry == b.entry; }),
                known.end());
    // The clean ROM: what corruption did to the code doesn't move where a function ends.
    bodies = function_bodies(rom.clean(), all.flags(), known, rom_where);
    guard_dirty = true;
}

void CorruptionSession::rebuildGuard()
{
    guard_dirty = false;
    size_t size = memory.size(Target::ROM);
    // Window flips are in play while they're off.
    std::erase_if(window_off, [&](uint32_t e) {
        return e >= size || e >= rom.clean().size() || memory.read(Target::ROM, e) == rom.clean()[e];
    });
    in_play = functions_in_play(engine.recipe(), window_off);
    auto calls = call_log.combined();
    HotMask mask = guard_mask(in_play, bodies, &calls);
    if (const uint8_t *now = memory.data(Target::ROM))
    {
        auto fix = guard_cleanup(mask, own_bytes(engine.recipe()), rom.clean(), { now, size });
        if (!fix.empty())
        {
            rom.settle(fix);
            note("Guard: put back " + std::to_string(fix.size()) + (fix.size() == 1 ? " byte" : " bytes") +
                 " other corruption had changed in " + guardedBy(fix.front().first));
        }
    }
    engine.set_guarded(mask);

    // The overlay and the hover text.
    function_list.clear();
    for (auto &[entry, body] : bodies)
    {
        if (body.code.empty())
            continue;
        ShownFunction f;
        f.entry = entry;
        f.address = rom_where.cpu_address(entry);
        f.label = names.label(entry);
        f.bytes = body.bytes;
        f.runs = body.runs();
        f.guarded = mask.is_hot(Target::ROM, entry);
        uint8_t now = entry < size ? memory.read(Target::ROM, entry) : 0;
        f.off = entry < rom.clean().size() && now != rom.clean()[entry] && (now == kRts || now == kRtl);
        function_list.push_back(std::move(f));
    }
    std::sort(function_list.begin(), function_list.end(),
              [](const ShownFunction &a, const ShownFunction &b) { return a.entry < b.entry; });
    function_shown = function_marks(function_list, mask, size);
}

std::string CorruptionSession::guardedBy(uint32_t offset) const
{
    // A function in play whose body holds it, else whichever function does.
    const ShownFunction *best = nullptr;
    for (auto &p : in_play)
        if (auto it = bodies.find(p.entry); it != bodies.end() && it->second.contains(offset))
            for (auto &f : function_list)
                if (f.entry == p.entry)
                    best = &f;
    if (!best)
        best = function_at(function_list, offset);
    if (!best)
        return "a function in play";
    std::string where = hex_address(best->entry, Target::ROM);
    if (best->address)
    {
        char cpu[16];
        std::snprintf(cpu, sizeof cpu, "$%02X:%04X", *best->address >> 16, *best->address & 0xFFFF);
        where = cpu;
    }
    return best->label.empty() ? where : where + " (" + best->label + ")";
}

std::vector<Survivor> CorruptionSession::survivors()
{
    std::lock_guard lock(mutex);
    if (!game_loaded || !memory.data(Target::ROM))
        return {};
    std::span<const uint8_t> now(memory.data(Target::ROM), memory.size(Target::ROM));
    auto flags = code_map.combined();
    return find_survivors(rom_blame, rom.clean(), now, flags.flags(), rom_where);
}

std::vector<Pin> CorruptionSession::pins()
{
    std::lock_guard lock(mutex);
    return engine.recipe().pins;
}

std::string CorruptionSession::pin(const std::vector<uint32_t> &offsets, const std::string &note_text)
{
    std::lock_guard lock(mutex);
    if (!game_loaded)
        return "No game loaded";
    if (!has_recipe)
        newRecipeLocked("Pinned");
    std::vector<Pin> pins;
    std::vector<std::pair<uint32_t, uint8_t>> keep;
    for (auto o : offsets)
    {
        if (o >= rom.clean().size() || rom.locked_mask().is_hot(Target::ROM, o))
            continue;
        uint8_t now = memory.read(Target::ROM, o);
        if (now == rom.clean()[o])
            continue; // nothing changed there to keep
        pins.push_back({ o, now, rom.clean()[o], note_text });
        keep.push_back({ o, now });
    }
    if (pins.empty())
        return "Nothing to pin: those bytes are clean";
    engine.add_pins(pins);
    rom.settle(keep); // no rollback undoes it now
    pinChanged();
    return {};
}

void CorruptionSession::unpin(const std::vector<uint32_t> &offsets)
{
    std::lock_guard lock(mutex);
    engine.remove_pins(offsets);
    pinChanged(); // the bytes stay as they are until the next run, watched again from their next change
}

void CorruptionSession::clearPatches()
{
    std::lock_guard lock(mutex);
    for (auto t : kAllTargets)
        engine.clear_patches(t);
}

std::string CorruptionSession::exportRom(const fs::path &path)
{
    std::vector<uint8_t> image;
    {
        std::lock_guard lock(mutex);
        if (!game_loaded)
            return "No game loaded";
        image = SnesCoreAccess::rom_image(true);
    }
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(image.data()), std::streamsize(image.size()));
    if (!out)
        return "Couldn't write " + path.string();
    return {};
}

void CorruptionSession::onFrame()
{
    std::lock_guard lock(mutex);
    engine.on_frame(rom);
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
