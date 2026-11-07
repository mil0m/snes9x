// snes9x-explore: the headless explorer (#45). From a kept moment, plays many variants unseen,
// each a recipe, pressing the moment's own buttons, scores each against the moment played
// without corruption, and keeps the best: their recipes, a kept moment of each run (plays back
// exactly in the app) and a picture strip, in one folder with candidates.json and gallery.html.
//
//   snes9x-explore <rom> <moment.s9xmoment> [--library DIR] [--user NAME] [--out DIR]
//                  [--variants N] [--minutes M] [--keep K] [--seconds S] [--seed N]
//
// --library: the shared library (the game's code maps, jump logs and call logs are read from
// it, and the exploration goes in <game folder>/explored/ unless --out says where). --variants
// (default 300) or --minutes (as many as fit; both: whichever comes first). --keep: how many
// candidates (24). --seconds: the window, at most the moment's length (20). Ctrl-C stops early
// and keeps what's there. Prints "progress <tried> <alive> <visible> <best find>" lines.
//
// Before the variants it measures normal play (#82): the moment 37 more times without
// corruption, most with one button press moved, and keeps only variants that go beyond it
// (a score over normal play's best, or code signals past it); "normal <done> <of>" lines
// meanwhile, then "normal best <score> <new functions> <silenced> <new code>".
// Plans: docs/corruption/2026-11-05-explorer-plan.md, 2026-11-06-normal-play-plan.md.
#include "headless_core.h"
#include "png_out.h"

#include "corrupt/calls.h"
#include "corrupt/code_map.h"
#include "corrupt/core_access.h"
#include "corrupt/core_memory.h"
#include "corrupt/engine.h"
#include "corrupt/envelope.h"
#include "corrupt/exploration.h"
#include "corrupt/explore.h"
#include "corrupt/function_body.h"
#include "corrupt/function_guard.h"
#include "corrupt/hook.h"
#include "corrupt/jump_tables.h"
#include "corrupt/library.h"
#include "corrupt/moment.h"
#include "corrupt/recipe_json.h"
#include "corrupt/rom_keeper.h"

#include "snes9x.h"
#include "memmap.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>

using namespace corrupt;
namespace fs = std::filesystem;

namespace
{

Engine engine;
CoreMemory memory;
MomentRecorder recorder;
RecordedMemory recorded{ memory, recorder };
RomKeeper rom{ recorded };
SnesCoreAccess core_access;
MomentPlayer player; // the candidates' moments checked at the end
std::atomic<bool> stop_asked{ false };

void frame_hook()
{
    if (player.playing())
        player.in_hook(memory);
    else if (engine.active())
    {
        recorder.hook_begin();
        engine.on_frame(rom);
        recorder.hook_end();
    }
    core_access.note_frame_start();
}

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

std::string local_time(const char *format)
{
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char buf[64];
    std::strftime(buf, sizeof buf, format, &tm);
    return buf;
}

std::string user_name()
{
    for (auto var : { "SNES9X_PLAYER", "USER", "USERNAME" })
        if (auto v = getenv(var); v && *v)
            return v;
    return "player";
}

std::optional<std::string> read_file(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

bool write_file(const fs::path &p, const std::string &bytes)
{
    return write_file_atomic(p, bytes).empty();
}

void on_signal(int) { stop_asked = true; }

struct EndState
{
    std::vector<uint8_t> state;
    std::vector<std::pair<uint32_t, uint8_t>> rom;
};

} // namespace

int main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IOLBF, 0);
    if (argc < 3)
    {
        fprintf(stderr,
                "usage: %s <rom> <moment.s9xmoment> [--library DIR] [--user NAME] [--out DIR] [--variants N] "
                "[--minutes M] [--keep K] [--seconds S] [--seed N]\n       %s <rom> --game-folder\n",
                argv[0], argv[0]);
        return 2;
    }
    std::string library_root, out_dir, user = user_name();
    long variants = -1;
    double minutes = 0;
    size_t keep = 24;
    double seconds = 20;
    uint64_t seed = std::random_device{}();
    for (int i = 3; i + 1 < argc; i += 2)
    {
        std::string flag = argv[i], value = argv[i + 1];
        if (flag == "--library")
            library_root = value;
        else if (flag == "--user")
            user = value;
        else if (flag == "--out")
            out_dir = value;
        else if (flag == "--variants")
            variants = std::stol(value);
        else if (flag == "--minutes")
            minutes = std::stod(value);
        else if (flag == "--keep")
            keep = size_t(std::stoul(value));
        else if (flag == "--seconds")
            seconds = std::stod(value);
        else if (flag == "--seed")
            seed = std::stoull(value);
        else
        {
            fprintf(stderr, "unknown option %s\n", flag.c_str());
            return 2;
        }
    }
    if (variants < 0 && minutes <= 0)
        variants = 300;
    bool game_folder = std::string(argv[2]) == "--game-folder";
    if (library_root.empty() && out_dir.empty() && !game_folder)
    {
        fprintf(stderr, "say where it goes: --library DIR or --out DIR\n");
        return 2;
    }

    if (!headless::init() || !headless::load_rom_file(argv[1]))
    {
        fprintf(stderr, "couldn't load %s\n", argv[1]);
        return 1;
    }
    SnesCoreAccess::watch_resets();
    rom.set_clock([] { return engine.current_frame(); });
    rom.game_loaded(SnesCoreAccess::locked_rom());
    engine.set_locked(rom.locked_mask());
    GameId game = current_game();
    if (game_folder) // the ROM's folder in a library (tools/explore_moments.sh)
    {
        printf("%s\n", game_folder_name(game.title, game.sha256).c_str());
        return 0;
    }

    auto bytes = read_file(argv[2]);
    std::string error;
    auto moment = bytes ? decode_moment(*bytes, error) : std::nullopt;
    if (!moment)
    {
        fprintf(stderr, "couldn't read the moment %s: %s\n", argv[2], bytes ? error.c_str() : "no such file");
        return 1;
    }
    if (!moment->game.sha256.empty() && moment->game.sha256 != game.sha256)
    {
        fprintf(stderr, "the moment was kept on another version of this ROM\n");
        return 1;
    }
    uint32_t window = uint32_t(std::min<double>(moment->frames.size(), std::max(2.0, seconds) * 60));
    if (window < 120)
    {
        fprintf(stderr, "the moment is too short to explore (%.1f s; at least 2 s)\n", moment->seconds());
        return 1;
    }
    ExploreStart start = start_from(*moment, window);
    std::span<const uint8_t> clean(rom.clean().data(), rom.clean().size());
    size_t size = rom.clean().size();

    // What the game is known to do: everyone's tracer files, then the moment itself.
    CodeMapKeeper code_maps;
    JumpLogKeeper jump_logs;
    CallKeeper call_logs;
    if (!library_root.empty())
    {
        Library lib(library_root);
        code_maps.reset(size, lib.load_code_maps(game, user, size));
        jump_logs.reset(lib.load_jump_logs(game, user, size));
        call_logs.reset(lib.load_call_logs(game, user, size));
    }
    else
        code_maps.reset(size);
    CodeMap map = code_maps.combined();
    std::vector<JumpTable> tables;
    std::vector<Function> functions;
    HotMask busy_mask;
    std::vector<uint8_t> aim_flags;
    engine.set_code_flags([&] {
        aim_flags = map.flags();
        for (auto &f : aim_flags)
            f &= codeflag::All;
        return std::span<const uint8_t>(aim_flags);
    });
    engine.set_jump_tables([&] { return tables; });
    engine.set_functions([&] { return functions; });
    engine.set_busy_code([&] { return busy_mask; });
    S9xCorruptFrameHook = &frame_hook;
    ExploreRig rig{ core_access, rom, engine, std::span<uint8_t>(map.data(), map.size()) };

    std::vector<uint32_t> strip_frames;
    for (int k = 0; k < 6; k++)
        strip_frames.push_back(window * (2 * k + 1) / 12);

    // The control: the moment's buttons without corruption, learning what the game runs.
    RunRecord control;
    ExploreKnows knows;
    {
        // The WRAM it reads most, for the ram family: counted in a plain run of the moment
        // (unseen frames don't count, by design).
        {
            WramAccessCounter reads;
            reads.start();
            core_access.load_state(start.state);
            rom.restore_clean();
            for (auto [o, v] : start.rom)
                rom.write_at_start(Target::ROM, o, v);
            for (auto &p : start.pads)
            {
                core_access.set_pads(p);
                headless::run_frames(1);
            }
            knows.wram = busy_wram(reads.read_counts());
            reads.stop();
            rom.restore_clean();
        }
        RomTracer tracer;
        tracer.start(map, &jump_logs.mine(), &call_logs.mine());
        PlayOptions learn;
        learn.keep = strip_frames;
        learn.keep_learning = true;
        learn.before_frame = [&](uint32_t f) {
            if (f)
                call_logs.end_frame();
        };
        control = play_variant(rig, start, nullptr, learn);
        tracer.stop();
    }
    if (control.crashed)
    {
        fprintf(stderr, "the moment itself doesn't play to its end without corruption (%s)\n", control.ended.c_str());
        return 1;
    }
    tables = find_jump_tables(jump_logs.combined(), clean, map.flags(), SnesCoreAccess::rom_offsets()).tables;
    functions = call_logs.functions();
    auto bodies = function_bodies(clean, map.flags(), functions, SnesCoreAccess::rom_addresses());
    std::vector<InPlay> busy;
    for (auto &f : busiest(functions, Engine::kBusiestFunctions))
        busy.push_back({ f.entry, false });
    busy_mask = guard_mask(busy, bodies, nullptr);
    if (!moment->recipe.empty())
        if (auto parsed = parse_recipe(moment->recipe, {}); parsed.recipe)
            knows.recipe = *parsed.recipe;
    knows.code_map = map.coverage().code > 0;
    knows.busy_code = busy_mask.count(Target::ROM) > 0;
    knows.jump_tables = !tables.empty();
    knows.functions = !functions.empty();
    printf("knows    %zu code bytes, %zu jump tables, %zu functions, %zu WRAM ranges; window %.1f s; seed %llu\n",
           map.coverage().code, tables.size(), functions.size(), knows.wram.size(), window / 60.0,
           (unsigned long long)seed);

    // Every run from here on records the functions it calls per frame, and the code it runs.
    CallLog calls_log;
    RomTracer tracer;
    tracer.start(map, nullptr, &calls_log);
    auto traced = [&](const ExploreStart &from, const Recipe *r, PlayOptions o, StateTrace &trace) {
        auto before = o.before_frame;
        auto after = o.after_frames;
        o.before_frame = [&, before](uint32_t f) {
            if (f == 0)
                calls_log.clear();
            else
                calls_log.end_frame();
            if (before)
                before(f);
        };
        o.after_frames = [&, after] {
            calls_log.end_frame();
            if (after)
                after();
        };
        auto run = play_variant(rig, from, r, o);
        trace = {};
        for (auto &[entry, n] : calls_log.frames_run())
            trace.calls[entry] = uint32_t(n);
        return run;
    };

    // Normal play (#82): the control again, 16 natural runs (one press shifted, dropped or
    // added: placebos) whose calls, pictures and code set normal play's range, then 20 more
    // placebos measured against it like variants: the most they reach is normal play's best.
    const int kNatural = 16, kBar = 20, kNormal = 1 + kNatural + kBar;
    // #84: after those, one more placebo every kPlaceboEvery tries keeps steadying the bars.
    // SNES9X_EXPLORE_PLACEBO_EVERY overrides it, 0 for none (for measuring).
    const uint32_t kPlaceboEvery =
        getenv("SNES9X_EXPLORE_PLACEBO_EVERY") ? uint32_t(atoi(getenv("SNES9X_EXPLORE_PLACEBO_EVERY"))) : 20;
    Envelope envelope;
    NormalBest normal;
    auto placebo = [&](int i) {
        ExploreStart p = start;
        p.pads = perturb(start.pads, seed + 0x5EED + uint64_t(i));
        return p;
    };
    {
        StateTrace trace;
        PlayOptions learn;
        learn.keep_learning = true; // code normal play runs isn't new
        int done = 0;
        for (int i = 0; i <= kNatural && !stop_asked; i++)
        {
            auto run = traced(i == 0 ? start : placebo(i), nullptr, learn, trace);
            if (!run.crashed) // a placebo that crashes isn't normal play
                envelope.add(run, trace);
            printf("normal %d %d\n", ++done, kNormal);
        }
        envelope.finish({});
        for (int i = 0; i < kBar && !stop_asked; i++)
        {
            auto run = traced(placebo(100 + i), nullptr, {}, trace);
            normal.add(score_run(run, control), envelope.measure(run, trace));
            printf("normal %d %d\n", ++done, kNormal);
        }
        normal.runs = uint32_t(done);
        printf("normal best %.3f %u %u %u\n", normal.score, normal.new_functions, normal.silenced, normal.new_code);
    }

    // Where it goes.
    Exploration e;
    e.game = game;
    e.moment = moment->name.empty() ? fs::path(argv[2]).stem().string() : moment->name;
    e.name = e.moment + " " + local_time("%d %b %H.%M");
    e.made_by = user;
    e.made_at = local_time("%Y-%m-%d %H:%M:%S");
    e.window = window;
    e.normal = normal;
    fs::path dir;
    if (!out_dir.empty())
    {
        dir = out_dir;
        std::error_code ec;
        fs::create_directories(dir, ec);
    }
    else
    {
        auto made = Library(library_root).new_exploration_dir(game, e.name);
        if (!made.error.empty())
        {
            fprintf(stderr, "%s\n", made.error.c_str());
            return 1;
        }
        dir = made.path;
    }
    headless::write_png((dir / "control.png").string(), headless::strip_of(control.kept));
    printf("folder   %s\n", dir.string().c_str());

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    recorder.set_config({ .keyframe_every = 1u << 30, .keyframes = 1, .lead = 0 });
    auto began = std::chrono::steady_clock::now();
    auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count(); };
    double last_write = 0;
    // Each try's score and signals, to judge them all again when the bars rise (#84).
    std::vector<std::pair<RunScore, Signals>> measured;
    auto save = [&](bool final) {
        e.seconds = elapsed();
        e.finished = final && !stop_asked;
        for (size_t i = 0; i < e.tried.size() && i < measured.size(); i++) // against the bars as they are now
            e.tried[i].find = float(judge(measured[i].first, measured[i].second, normal).find);
        for (size_t i = 0; i < e.candidates.size(); i++)
            e.candidates[i].rank = uint32_t(i + 1);
        write_file(dir / "candidates.json", encode_exploration(e));
        if (final)
            write_file(dir / "gallery.html", gallery_html(e, [&](const std::string &f) { return read_file(dir / f); }));
        last_write = elapsed();
    };
    save(false);

    std::map<uint32_t, EndState> ends; // each candidate's run as it ended, for the check
    auto drop = [&](const Candidate &gone) {
        std::error_code ec;
        fs::remove(dir / gone.moment_file, ec);
        fs::remove(dir / gone.strip_file, ec);
        fs::remove(dir / (fs::path(gone.moment_file).stem().string() + ".json"), ec);
        ends.erase(gone.id);
    };
    // A placebo between the variants: judged against the bars so far, then one of them. When a
    // bar rises, every candidate is judged again and those no longer past it leave.
    auto later_placebo = [&] {
        StateTrace trace;
        auto run = traced(placebo(1000 + int(normal.later)), nullptr, {}, trace);
        auto s = score_run(run, control);
        auto g = envelope.measure(run, trace);
        bool kept = judge(s, g, normal).kept;
        normal.later++;
        normal.later_kept += kept;
        if (normal.add(s, g))
        {
            for (auto &c : e.candidates)
                c.verdict = judge(c.score, c.measured, normal);
            for (auto &c : e.candidates)
                if (!c.verdict.kept)
                    drop(c);
            std::erase_if(e.candidates, [](const Candidate &c) { return !c.verdict.kept; });
            std::stable_sort(e.candidates.begin(), e.candidates.end(),
                             [](const Candidate &a, const Candidate &b) { return a.verdict.find > b.verdict.find; });
        }
        e.normal = normal;
        printf("placebo %u %u%s\n", normal.later, normal.later_kept, kept ? " kept" : "");
    };
    const uint32_t batch = 50;
    uint32_t tried = 0;
    auto enough = [&] {
        return stop_asked || (variants >= 0 && long(tried) >= variants) || (minutes > 0 && elapsed() >= minutes * 60);
    };
    for (uint32_t b = 0; !enough(); b++)
    {
        auto vs = make_variants(knows, rom, batch, seed + uint64_t(b) * 0x9E3779B97F4A7C15ULL, window);
        for (auto &v : vs)
        {
            if (enough())
                break;
            v.id = tried;
            v.recipe.name = std::string("Explored ") + family_name(v.family) + " " + std::to_string(v.id + 1);
            recorder.reset();
            std::optional<Moment> kept_moment;
            EndState end;
            PlayOptions o;
            o.keep = strip_frames;
            o.before_frame = [&](uint32_t) { recorder.before_frame(core_access, memory, clean, core_access.timeline()); };
            o.after_frames = [&] {
                kept_moment = recorder.take();
                end.state = core_access.save_state();
                end.rom.clear();
                for (uint32_t i = 0; i < size; i++)
                    if (uint8_t v = memory.read(Target::ROM, i); v != clean[i])
                        end.rom.emplace_back(i, v);
            };
            StateTrace trace;
            auto run = traced(start, &v.recipe, o, trace);
            auto s = score_run(run, control);
            std::vector<AddressRange> own;
            for (auto &rule : v.recipe.rules)
                own.push_back(rule.range);
            auto g = envelope.measure(run, trace, own);
            auto verdict = judge(s, g, normal);
            measured.push_back({ s, g });
            tried++;
            auto &f = e.families[size_t(v.family)];
            f.runs++;
            f.alive += s.alive;
            f.visible += s.alive && s.different >= kVisible;
            e.variants = tried;
            e.alive += s.alive;
            e.visible += s.alive && s.different >= kVisible;
            e.tried.push_back({ v.family,
                                !s.alive                    ? Tried::Stopped
                                : s.different >= kVisible   ? Tried::Visible
                                                            : Tried::Unchanged,
                                float(s.score), float(verdict.find) });
            bool worth = verdict.kept && kept_moment &&
                         (e.candidates.size() < keep || verdict.find > e.candidates.back().verdict.find);
            if (worth)
            {
                Candidate c;
                c.id = v.id;
                c.family = v.family;
                c.what = v.what;
                c.recipe = v.recipe;
                c.score = s;
                c.measured = g;
                c.verdict = verdict;
                std::string stem = std::to_string(v.id + 1) + " " + family_name(v.family);
                c.moment_file = stem + ".s9xmoment";
                c.strip_file = stem + ".png";
                Moment &m = *kept_moment;
                m.name = v.recipe.name + " (from " + e.moment + ")";
                m.game = game;
                m.recipe = write_recipe(v.recipe);
                m.recipe_name = v.recipe.name;
                m.seed = v.recipe.seed;
                m.made_by = user + " (explorer)";
                m.made_at = e.made_at;
                write_file(dir / c.moment_file, encode_moment(m));
                write_file(dir / (stem + ".json"), write_recipe(v.recipe));
                headless::write_png((dir / c.strip_file).string(), headless::strip_of(run.kept));
                auto at = std::upper_bound(e.candidates.begin(), e.candidates.end(), verdict.find,
                                           [](double v, const Candidate &x) { return v > x.verdict.find; });
                ends[c.id] = std::move(end);
                e.candidates.insert(at, std::move(c));
                if (e.candidates.size() > keep)
                {
                    drop(e.candidates.back());
                    e.candidates.pop_back();
                }
            }
            if (kPlaceboEvery && tried % kPlaceboEvery == 0)
                later_placebo();
            if (tried % 10 == 0 || worth)
                printf("progress %u %u %u %.3f\n", tried, e.alive, e.visible,
                       e.candidates.empty() ? 0.0 : e.candidates.front().verdict.find);
            if (elapsed() - last_write >= 30)
                save(false);
        }
    }
    tracer.stop();
    // Every candidate's moment plays back to exactly its run (#47's check).
    size_t exact = 0;
    for (auto &c : e.candidates)
    {
        auto file = read_file(dir / c.moment_file);
        std::string why;
        auto m = file ? decode_moment(*file, why) : std::nullopt;
        if (!m)
            continue;
        // "Now" is the run's end: its state, and its ROM.
        auto &end = ends[c.id];
        core_access.load_state(end.state);
        rom.restore_clean();
        for (auto [o, v] : end.rom)
            rom.write_at_start(Target::ROM, o, v);
        auto check = check_moment(*m, player, core_access, memory, clean);
        exact += check.exact;
        c.exact = check.exact;
        if (!check.exact)
            printf("check    %s: %s\n", c.moment_file.c_str(), check.text.c_str());
    }
    printf("check    %zu of %zu candidates' moments play back exactly\n", exact, e.candidates.size());
    S9xCorruptFrameHook = nullptr;
    save(true);
    printf("done     %u variants in %.0f s: %u kept running, %u visibly changed; %zu candidates in %s\n", e.variants,
           e.seconds, e.alive, e.visible, e.candidates.size(), dir.string().c_str());
    return 0;
}
