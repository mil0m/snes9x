#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/code_map.h"
#include "corrupt/explore.h"
#include "corrupt/exploration.h"
#include "corrupt/library.h"
#include "corrupt/recipe_json.h"
#include "corrupt/rule_text.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <unistd.h>

using namespace corrupt;

namespace
{

Picture solid(uint8_t r, uint8_t g, uint8_t b, int w = kThumbW, int h = kThumbH)
{
    Picture p;
    p.w = uint16_t(w);
    p.h = uint16_t(h);
    for (int i = 0; i < w * h; i++)
        p.rgb.insert(p.rgb.end(), { r, g, b });
    return p;
}

// A picture with something on it: stripes, and a block at `at` that moves.
Picture scene(int at, uint8_t tint = 0)
{
    Picture p = solid(20, 40, 60);
    for (int y = 0; y < kThumbH; y++)
        for (int x = 0; x < kThumbW; x++)
        {
            uint8_t *c = &p.rgb[(size_t(y) * kThumbW + x) * 3];
            if ((x / 4 + y / 4) % 2)
                c[0] = uint8_t(200 - tint), c[1] = 180, c[2] = uint8_t(40 + tint);
            if (x >= at % kThumbW && x < at % kThumbW + 6 && y < 10)
                c[0] = 255, c[1] = 255, c[2] = 255;
        }
    return p;
}

RunRecord record(std::vector<Picture> pictures, bool crashed = false)
{
    RunRecord r;
    r.thumbs = std::move(pictures);
    for (auto &p : r.thumbs)
        r.hashes.push_back(picture_hash(p));
    r.frames = uint32_t(r.thumbs.size());
    r.crashed = crashed;
    return r;
}

RunRecord moving(int frames, uint8_t tint = 0, int from = 0)
{
    std::vector<Picture> ps;
    for (int f = from; f < from + frames; f++)
        ps.push_back(scene(f, tint));
    return record(ps);
}

// A game the explorer can play: its picture comes from its work RAM (a tile of 4 KB drawn
// as 64 x 56, and a block moved by the frame counter), it stops when a byte says so, and it
// "runs code" where another byte points (the tracer's flags).
struct Game : CoreAccess
{
    FakeMemory &mem;
    Engine &engine;
    MemoryAccess &through; // what the engine writes to (the ROM keeper)
    std::span<uint8_t> flags;
    Pads held{};
    int unseen_depth = 0;
    uint64_t loads = 0;

    Game(FakeMemory &m, Engine &e, MemoryAccess &t, std::span<uint8_t> f) : mem(m), engine(e), through(t), flags(f) {}
    std::vector<uint8_t> &wram() { return mem.of(Target::WRAM); }

    std::vector<uint8_t> save_state() override { return wram(); }
    bool load_state(const std::vector<uint8_t> &s) override
    {
        if (s.size() != wram().size())
            return false;
        wram() = s;
        loads++;
        return true;
    }
    FrameSignals sample() override
    {
        FrameSignals s;
        s.cpu_halted = wram()[0x50] == 0xFF;
        s.input = held[0];
        Picture p;
        picture(p, true);
        s.picture_hash = picture_hash(p);
        return s;
    }
    void begin_unseen() override { unseen_depth++; }
    void end_unseen() override { unseen_depth--; }
    void run_unseen_frame(bool) override
    {
        if (engine.active())
            engine.on_frame(through);
        auto &w = wram();
        if (w[0x50] == 0xFF)
            return; // stopped
        w[0] = uint8_t(w[0] + 1 + (held[0] & 1));
        flags[0x100 + w[0x60]] |= codeflag::Opcode;
    }
    Pads pads() override { return held; }
    void set_pads(const Pads &p) override { held = p; }
    bool picture(Picture &out, bool thumbnail) override
    {
        auto &w = wram();
        int scale = thumbnail ? 1 : 4;
        out.w = uint16_t(kThumbW * scale);
        out.h = uint16_t(kThumbH * scale);
        out.rgb.assign(size_t(out.w) * out.h * 3, 0);
        for (int y = 0; y < out.h; y++)
            for (int x = 0; x < out.w; x++)
            {
                int tx = x / scale, ty = y / scale;
                uint8_t v = w[0x1000 + ((ty * kThumbW + tx) & 0xFFF)];
                uint8_t *c = &out.rgb[(size_t(y) * out.w + x) * 3];
                c[0] = v;
                c[1] = uint8_t(v * 3);
                c[2] = uint8_t(255 - v);
                if (tx >= w[0] % kThumbW && tx < w[0] % kThumbW + 4 && ty < 8)
                    c[0] = c[1] = c[2] = 255;
            }
        return true;
    }
};

struct Rig
{
    FakeMemory mem;
    RomKeeper rom{ mem };
    Engine engine;
    std::vector<uint8_t> flags = std::vector<uint8_t>(mem.size(Target::ROM), 0);
    Game game{ mem, engine, rom, flags };
    ExploreRig rig{ game, rom, engine, flags };
    ExploreStart start;

    Rig()
    {
        auto &w = mem.of(Target::WRAM);
        for (size_t i = 0; i < 0x1000; i++)
            w[0x1000 + i] = uint8_t((i / 64 + i % 64) % 2 ? 200 : 30); // a checkerboard
        auto &r = mem.of(Target::ROM);
        for (size_t i = 0; i < r.size(); i++)
            r[i] = uint8_t(i);
        rom.game_loaded({});
        start.state = game.save_state();
        start.rom = { { 0x2000, 0x99 } }; // the moment's ROM had one byte changed
        for (int f = 0; f < 600; f++)
        {
            Pads p{};
            p[0] = uint16_t(f % 30 < 15 ? 0x0080 : 0);
            start.pads.push_back(p);
        }
    }

    Recipe one(Rule r)
    {
        Recipe recipe;
        recipe.name = "test";
        recipe.seed = 3;
        r.enabled = true;
        recipe.rules = { r };
        return recipe;
    }
};

Rule wram_rule(uint32_t first, uint32_t last, OpType type, uint32_t every = 1, uint32_t count = 8)
{
    Rule r;
    r.name = "r";
    r.range = { Target::WRAM, first, last };
    r.op.type = type;
    r.op.min = 0;
    r.op.max = 255;
    r.count = count;
    r.when = { .type = TriggerType::EveryFrames, .every = every };
    return r;
}

} // namespace

TEST_SUITE("the headless explorer (#45)")
{
    TEST_CASE("pictures: how much differs, and how much is on the screen")
    {
        auto a = scene(0), b = scene(0);
        CHECK(differing_share(a, b) == 0.0);
        CHECK(differing_share(a, solid(0, 0, 0)) > 0.9);
        CHECK(differing_share(a, solid(0, 0, 0, 10, 10)) == 1.0); // another size
        auto black = detail_of(solid(0, 0, 0));
        CHECK(black.colours == 1);
        CHECK(black.edges == 0.0);
        auto busy = detail_of(scene(0));
        CHECK(busy.colours >= 3);
        CHECK(busy.edges > 0.1);
        CHECK(picture_hash(a) == picture_hash(b));
        CHECK(picture_hash(a) != picture_hash(scene(1)));
    }

    TEST_CASE("the score: alive x different x lively, with a bonus for new code")
    {
        auto control = moving(600);
        // The same as without corruption: nothing to see.
        auto same = score_run(moving(600), control);
        CHECK(same.alive);
        CHECK(same.different == 0.0);
        CHECK(same.score == 0.0);
        // Another look that still moves: what the explorer is for.
        auto tinted = score_run(moving(600, 120), control);
        CHECK(tinted.alive);
        CHECK(tinted.different > 0.3);
        CHECK(tinted.lively == doctest::Approx(1.0));
        CHECK(tinted.score == doctest::Approx(1.0)); // visible past 10%: more doesn't count
        // A little different: a little visible.
        std::vector<Picture> small;
        for (int f = 0; f < 600; f++)
        {
            auto p = scene(f);
            for (int x = 0; x < 3; x++) // 3 x 56 pixels of 3584: about 5%
                for (int y = 0; y < kThumbH; y++)
                    p.rgb[(size_t(y) * kThumbW + 40 + x) * 3] = 0, p.rgb[(size_t(y) * kThumbW + 40 + x) * 3 + 2] = 255;
            small.push_back(p);
        }
        auto little = score_run(record(small), control);
        CHECK(little.alive);
        CHECK(little.score > 0.2);
        CHECK(little.score < 0.8);
        // Tile garbage: far more edges than the scene. Moving, different, and still low.
        std::vector<Picture> noise;
        uint32_t seed = 1;
        for (int f = 0; f < 600; f++)
        {
            Picture p = solid(0, 0, 0);
            for (auto &v : p.rgb)
                v = uint8_t((seed = seed * 1103515245 + 12345) >> 16);
            noise.push_back(p);
        }
        auto garbage = score_run(record(noise), control);
        CHECK(garbage.alive);
        CHECK(garbage.edges_ratio > 2);
        CHECK(garbage.score < 0.3);
        // A black screen differs everywhere, and scores next to nothing.
        std::vector<Picture> ps;
        for (int f = 0; f < 600; f++)
            ps.push_back(f < 300 ? scene(f) : solid(0, 0, 0));
        auto black = score_run(record(ps), control);
        CHECK(black.different > 0.9);
        CHECK(black.lively < 0.1);
        CHECK(black.score < 0.1 * tinted.score);
        // A picture that stopped while the control moves: a freeze, not alive.
        ps.clear();
        for (int f = 0; f < 600; f++)
            ps.push_back(scene(std::min(f, 400), 120));
        auto frozen = score_run(record(ps), control);
        CHECK(frozen.froze);
        CHECK(!frozen.alive);
        CHECK(frozen.score == 0.0);
        // A crash, or a run cut short: not alive.
        auto crashed = moving(600, 120);
        crashed.crashed = true;
        CHECK(score_run(crashed, control).score == 0.0);
        CHECK(!score_run(moving(300, 120), control).alive);
        // New code: a bonus on top, never instead.
        auto with_code = moving(600, 120);
        with_code.new_code = 192; // log2(1 + 3) = 2: x1.5
        CHECK(score_run(with_code, control).score == doctest::Approx(tinted.score * 1.5));
        auto code_only = moving(600);
        code_only.new_code = 5000;
        CHECK(score_run(code_only, control).score == 0.0);
    }

    TEST_CASE("variants: every family the knowledge allows, in equal shares, valid and repeatable")
    {
        FakeMemory mem;
        ExploreKnows knows;
        auto plain = make_variants(knows, mem, 30, 7, 600);
        REQUIRE(plain.size() == 30);
        for (auto &v : plain)
            CHECK(v.family == Family::Ram); // nothing else known
        Recipe own;
        own.name = "own";
        own.seed = 11;
        own.rules = { wram_rule(0x2000, 0x2FFF, OpType::Add, 10, 2) };
        own.rules[0].op.min = -2;
        own.rules[0].op.max = 2;
        knows.recipe = own;
        knows.code_map = knows.busy_code = knows.jump_tables = knows.functions = true;
        auto all = make_variants(knows, mem, 70, 7, 600);
        std::map<Family, int> per;
        std::set<uint64_t> seeds;
        for (auto &v : all)
        {
            per[v.family]++;
            seeds.insert(v.recipe.seed);
            CHECK(!v.what.empty());
            CHECK(v.recipe.name.find(family_name(v.family)) != std::string::npos);
            for (auto &r : v.recipe.rules)
            {
                auto problem = validate_rule(r, mem);
                CHECK_MESSAGE(!problem, family_name(v.family), ": ", problem.value_or(""), " in ", describe_rule(r));
            }
        }
        CHECK(per.size() == kFamilies);
        for (auto &[f, n] : per)
            CHECK(n == 10);
        CHECK(seeds.size() > 60); // each its own seed
        // Reroll keeps the rules; nudge changes one; code aims inside busy functions.
        for (auto &v : all)
        {
            if (v.family == Family::Reroll)
                CHECK(describe_rule(v.recipe.rules[0]) == describe_rule(own.rules[0]));
            if (v.family == Family::Code)
                CHECK(v.recipe.rules[0].aim.in_busy_functions);
            if (v.family == Family::Swap || v.family == Family::Kill)
            {
                CHECK(v.recipe.rules[0].when.type == TriggerType::OnceAtFrame);
                CHECK(v.recipe.rules[0].when.frame <= 200);
            }
        }
        int nudged = 0;
        for (auto &v : all)
            nudged += v.family == Family::Nudge && describe_rule(v.recipe.rules[0]) != describe_rule(own.rules[0]);
        CHECK(nudged >= 7); // a strength nudge at full strength can leave the words the same
        // The same seed, the same variants.
        auto again = make_variants(knows, mem, 70, 7, 600);
        for (size_t i = 0; i < all.size(); i++)
            CHECK(write_recipe(again[i].recipe) == write_recipe(all[i].recipe));
        CHECK(write_recipe(make_variants(knows, mem, 70, 8, 600)[3].recipe) != write_recipe(all[3].recipe));
    }

    TEST_CASE("the ram family aims at the WRAM the game reads most, clear of the direct page and stack")
    {
        std::vector<uint32_t> reads(0x20000, 0);
        for (uint32_t i = 0; i < 0x200; i++)
            reads[i] = 1000; // direct page and stack: left out
        for (uint32_t i = 0x1E00; i < 0x2000; i++)
            reads[i] = 1000;
        for (uint32_t i = 0x0400; i < 0x0600; i++) // two blocks that touch: one range
            reads[i] = 50;
        for (uint32_t i = 0x7000; i < 0x7100; i++)
            reads[i] = 80;
        reads[0x9000] = 1; // too few to make the cut
        auto r = busy_wram(reads, 3);
        REQUIRE(r.size() == 2);
        CHECK(r[0].first == 0x0400);
        CHECK(r[0].last == 0x05FF);
        CHECK(r[1].first == 0x7000);
        CHECK(r[1].last == 0x70FF);
        CHECK(busy_wram(std::vector<uint32_t>(0x20000, 0)).empty());
        // The variants pick inside them.
        FakeMemory mem;
        ExploreKnows knows;
        knows.wram = r;
        for (auto &v : make_variants(knows, mem, 60, 3, 600))
        {
            auto &range = v.recipe.rules[0].range;
            if (range.target != Target::WRAM)
                continue;
            bool inside = (range.first >= 0x400 && range.last <= 0x5FF) || (range.first >= 0x7000 && range.last <= 0x70FF);
            CHECK_MESSAGE(inside, describe_rule(v.recipe.rules[0]));
        }
    }

    TEST_CASE("playing a variant: from the moment's start with its buttons, and everything put back")
    {
        Rig rig;
        // The live game is somewhere else when the explorer plays.
        rig.mem.of(Target::WRAM)[0] = 77;
        rig.mem.of(Target::WRAM)[0x1234] = 5;
        auto live = rig.game.save_state();
        rig.rom.write(Target::ROM, 0x3000, 0x42);
        auto rom_live = rig.mem.of(Target::ROM);
        rig.flags[0x10] = codeflag::Data;
        auto flags_live = rig.flags;

        PlayOptions keep;
        keep.keep = { 0, 299, 599 };
        auto control = play_variant(rig.rig, rig.start, nullptr, keep);
        CHECK(control.frames == 600);
        CHECK(!control.crashed);
        CHECK(control.kept.size() == 3);
        CHECK(control.kept[0].w == kThumbW * 4);
        CHECK(control.new_code == 1); // the start points at one byte the map didn't know
        // The buttons were the moment's: the block moved 1 a frame, 2 with the button held.
        CHECK(control.thumbs.size() == 600);
        auto again = play_variant(rig.rig, rig.start, nullptr);
        CHECK(again.hashes == control.hashes); // the same start, the same buttons, the same run
        // Put back: the live game, its ROM, the map, the engine off, not unseen.
        CHECK(rig.game.save_state() == live);
        CHECK(rig.mem.of(Target::ROM) == rom_live);
        CHECK(rig.flags == flags_live);
        CHECK(!rig.engine.active());
        CHECK(rig.game.unseen_depth == 0);

        // Noise on the picture's tile: alive, different, lively.
        auto noise = rig.one(wram_rule(0x1000, 0x1FFF, OpType::Random, 1, 64));
        auto run = play_variant(rig.rig, rig.start, &noise);
        auto s = score_run(run, control);
        CHECK(s.alive);
        CHECK(s.different > 0.2);
        CHECK(s.score > 0.1);
        CHECK(rig.game.save_state() == live);
        CHECK(rig.mem.of(Target::ROM) == rom_live);

        // Writing the stop byte: a crash, which ends the run.
        auto stop = rig.one(wram_rule(0x50, 0x50, OpType::Freeze));
        stop.rules[0].op.value = 0xFF;
        auto dead = play_variant(rig.rig, rig.start, &stop);
        CHECK(dead.crashed);
        CHECK(dead.frames < 600);
        CHECK(!dead.ended.empty());
        CHECK(score_run(dead, control).score == 0.0);

        // Code never run before: counted, and the map put back.
        auto jump = rig.one(wram_rule(0x60, 0x60, OpType::Random, 5, 1));
        auto wander = play_variant(rig.rig, rig.start, &jump);
        CHECK(wander.new_code > 10);
        CHECK(rig.flags == flags_live);
    }

    TEST_CASE("an exploration's file keeps the candidates and their recipes; the gallery shows them; the library lists them")
    {
        Exploration e;
        e.name = "The HUD eats Max 5 Nov 21.13";
        e.game = { "GOOF TROOP", "abcdef0123456789", "1234abcd" };
        e.moment = "The HUD eats Max";
        e.made_by = "mikael";
        e.made_at = "2026-11-05 21:13:00";
        e.window = 1200;
        e.variants = 300;
        e.alive = 280;
        e.visible = 41;
        e.seconds = 610;
        e.finished = true;
        e.families[size_t(Family::Kill)] = { 43, 42, 9 };
        Candidate c;
        c.rank = 1;
        c.id = 17;
        c.family = Family::Kill;
        c.what = "rom: turn busy functions off x1, once at frame 121 <&>";
        c.recipe.name = "Explored kill 18";
        c.recipe.seed = 99;
        Rule r;
        r.name = "kill";
        r.range = { Target::ROM, 0, 0x7FFFF };
        r.op.type = OpType::Kill;
        r.when = { .type = TriggerType::OnceAtFrame, .frame = 121 };
        c.recipe.rules = { r };
        c.score.score = 0.74;
        c.score.different = 0.86;
        c.score.lively = 0.86;
        c.score.new_code = 12;
        c.moment_file = "01 kill.s9xmoment";
        c.strip_file = "01 kill.png";
        c.exact = true;
        e.candidates = { c };

        std::string error;
        auto back = decode_exploration(encode_exploration(e), error);
        REQUIRE_MESSAGE(back, error);
        CHECK(back->name == e.name);
        CHECK(back->game.sha256 == e.game.sha256);
        CHECK(back->variants == 300);
        CHECK(back->visible == 41);
        CHECK(back->finished);
        CHECK(back->families[size_t(Family::Kill)].visible == 9);
        REQUIRE(back->candidates.size() == 1);
        auto &b = back->candidates[0];
        CHECK(b.family == Family::Kill);
        CHECK(b.score.score == doctest::Approx(0.74));
        CHECK(b.score.new_code == 12);
        CHECK(b.moment_file == "01 kill.s9xmoment");
        CHECK(b.exact == std::optional<bool>(true));
        CHECK(write_recipe(b.recipe) == write_recipe(c.recipe));
        CHECK(!decode_exploration("{", error));
        CHECK(!decode_exploration(R"({"version": 9})", error));

        auto html = gallery_html(e, [](const std::string &f) -> std::optional<std::string> {
            if (f == "01 kill.png")
                return std::string("\x89PNG fake");
            return std::nullopt;
        });
        CHECK(html.find("data:image/png;base64,iVBORyBmYWtl") != std::string::npos);
        CHECK(html.find("&lt;&amp;&gt;") != std::string::npos); // escaped
        CHECK(html.find("<&>") == std::string::npos);
        CHECK(html.find("280 kept running") != std::string::npos);
        CHECK(html.find("plays back exactly") != std::string::npos);

        namespace fs = std::filesystem;
        auto root = fs::temp_directory_path() / ("explore-test-" + std::to_string(::getpid()));
        fs::remove_all(root);
        Library lib(root);
        auto dir = lib.new_exploration_dir(e.game, e.name);
        REQUIRE(dir.error.empty());
        CHECK(dir.path == lib.explored_dir(e.game) / e.name);
        CHECK(lib.list_explorations(e.game).empty()); // nothing written yet: not listed
        { std::ofstream(dir.path / "candidates.json") << encode_exploration(e); }
        auto again = lib.new_exploration_dir(e.game, e.name);
        CHECK(again.path.filename() == e.name + " 2");
        Exploration older = e;
        older.made_at = "2026-11-04 08:00:00";
        { std::ofstream(again.path / "candidates.json") << encode_exploration(older); }
        auto list = lib.list_explorations(e.game);
        REQUIRE(list.size() == 2);
        CHECK(list[0].dir == dir.path); // newest first
        CHECK(lib.delete_exploration(again.path).empty());
        CHECK(lib.list_explorations(e.game).size() == 1);
        CHECK(!lib.delete_exploration(root).empty()); // not an exploration's folder
        fs::remove_all(root);
    }

    TEST_CASE("what the charts show (#80): each kind's outcomes, the scores' spread, when the finds turned up")
    {
        Exploration e;
        e.variants = 12;
        // In the order tried: code finds two good ones, kill one, ram changes nothing much.
        e.tried = {
            { Family::Code, Tried::Visible, 0.10f },  { Family::Kill, Tried::Stopped, 0 },
            { Family::Ram, Tried::Unchanged, 0 },     { Family::Code, Tried::Visible, 1.20f },
            { Family::Kill, Tried::Visible, 0.40f },  { Family::Ram, Tried::Unchanged, 0 },
            { Family::Code, Tried::Stopped, 0 },      { Family::Kill, Tried::Unchanged, 0 },
            { Family::Ram, Tried::Visible, 0.02f },   { Family::Code, Tried::Visible, 2.50f },
            { Family::Kill, Tried::Stopped, 0 },      { Family::Ram, Tried::Unchanged, 0 },
        };
        Candidate a, b;
        a.id = 9;
        a.family = b.family = Family::Code;
        a.score.score = 2.5;
        b.id = 3;
        b.score.score = 1.2;
        e.candidates = { a, b };
        auto st = stats_of(e);
        CHECK(st.detailed);
        CHECK(st.tried == 12);
        REQUIRE(st.kinds.size() == 3);
        CHECK(st.kinds[0].family == Family::Code); // 3 of 4 visible
        CHECK(st.kinds[0].visible == 3);
        CHECK(st.kinds[0].stopped == 1);
        CHECK(st.kinds[0].kept == 2);
        CHECK(st.kinds[1].family == Family::Ram); // 1 of 4 visible, like kill, but it never stopped
        CHECK(st.kinds[2].family == Family::Kill);
        CHECK(st.kinds[2].stopped == 2);
        CHECK(st.kinds[2].unchanged == 1);
        CHECK(st.zero_scores == 4); // ran and changed nothing
        uint32_t binned = 0;
        for (auto &bin : st.bins)
        {
            binned += bin.count;
            CHECK(bin.kept_side == (bin.lo >= kMinScore));
        }
        CHECK(binned == 5); // the visible ones
        auto bin_of = [&](double v) {
            for (auto &bin : st.bins)
                if (v >= bin.lo && v < bin.hi)
                    return bin;
            return ScoreBin{};
        };
        CHECK(bin_of(2.5).count == 1);
        CHECK(bin_of(0.02).count == 1);
        REQUIRE(st.finds.size() == 3); // at or above the keep line
        CHECK(st.finds[0].at == 3);
        CHECK(st.finds[0].kept);
        CHECK(st.finds[1].at == 4);
        CHECK(!st.finds[1].kept); // 0.4 isn't among the candidates now
        CHECK(st.finds[2].at == 9);
        // The best so far: where it rose.
        REQUIRE(st.best.size() == 3);
        CHECK(st.best[0].first == 0);
        CHECK(st.best[0].second == doctest::Approx(0.1).epsilon(1e-6));
        CHECK(st.best[1].first == 3);
        CHECK(st.best[2].first == 9);

        // The file keeps them.
        std::string error;
        auto back = decode_exploration(encode_exploration(e), error);
        REQUIRE(back);
        REQUIRE(back->tried.size() == 12);
        CHECK(back->tried[3].family == Family::Code);
        CHECK(back->tried[3].outcome == Tried::Visible);
        CHECK(back->tried[3].score == doctest::Approx(1.2f));
        CHECK(back->tried[1].outcome == Tried::Stopped);

        // An older file: no tries, the kinds from the family totals, nothing else.
        Exploration old;
        old.variants = 100;
        old.families[size_t(Family::Swap)] = { 50, 45, 10 };
        old.families[size_t(Family::Kill)] = { 50, 40, 20 };
        auto os = stats_of(old);
        CHECK(!os.detailed);
        REQUIRE(os.kinds.size() == 2);
        CHECK(os.kinds[0].family == Family::Kill);
        CHECK(os.kinds[0].stopped == 10);
        CHECK(os.kinds[0].unchanged == 20);
        CHECK(os.kinds[0].visible == 20);
        CHECK(os.bins.empty());
        CHECK(os.finds.empty());
    }

    TEST_CASE("the gallery draws the charts (#80): one mark per bar and find, each with its numbers")
    {
        Exploration e;
        e.game.title = "POCKY ROCKY";
        e.moment = "Temple";
        e.variants = 3;
        e.tried = { { Family::Code, Tried::Visible, 1.5f }, { Family::Kill, Tried::Stopped, 0 },
                    { Family::Ram, Tried::Unchanged, 0 } };
        auto html = gallery_html(e, [](const std::string &) { return std::nullopt; });
        CHECK(html.find("<svg") != std::string::npos);
        CHECK(html.find("What each kind did") != std::string::npos);
        CHECK(html.find("<title>code: 1 tried, 1 visibly changed") != std::string::npos);
        CHECK(html.find("When the good finds turned up") != std::string::npos);
        CHECK(html.find("<title>#1 (code): score 1.50") != std::string::npos);
        // An older file says why two charts are missing.
        Exploration old = e;
        old.tried.clear();
        old.families[size_t(Family::Code)] = { 1, 1, 1 };
        auto older = gallery_html(old, [](const std::string &) { return std::nullopt; });
        CHECK(older.find("What each kind did") != std::string::npos);
        CHECK(older.find("explored before the explorer kept every try") != std::string::npos);
    }
    TEST_CASE("normal play's best (#82): the most any bar-setting placebo reached, alive ones only")
    {
        NormalBest best;
        RunScore a, b, dead;
        Signals ga, gb, gdead;
        a.alive = b.alive = true;
        a.score = 0.4, a.new_code = 10, ga.new_functions = 1;
        b.score = 0.9, b.new_code = 3, gb.silenced = 2;
        dead.score = 5, dead.new_code = 900, gdead.new_functions = 50; // a placebo that crashed sets nothing
        best.add(a, ga);
        best.add(b, gb);
        best.add(dead, gdead);
        CHECK(best.score == doctest::Approx(0.9));
        CHECK(best.new_code == 10);
        CHECK(best.new_functions == 1);
        CHECK(best.silenced == 2);
        CHECK(best.keep_line() == doctest::Approx(0.9));
        NormalBest quiet; // a scene where buttons change nothing: the old floor
        quiet.runs = 36;
        CHECK(quiet.keep_line() == doctest::Approx(kMinScore));
    }

    TEST_CASE("the verdict (#82): kept when it looks different beyond normal play, or acts different and shows")
    {
        NormalBest best;
        best.runs = 36;
        best.score = 0.6;
        best.new_functions = 0;
        best.silenced = 1;
        best.new_code = 40;
        RunScore s;
        s.alive = true;
        Signals g;

        s.score = 0.5, s.different = 0.3; // as different as normal play gets: nothing
        auto v = judge(s, g, best);
        CHECK(!v.kept);
        CHECK(v.acts == 0);
        CHECK(v.find < 1);

        s.score = 1.2; // twice normal play's best
        v = judge(s, g, best);
        CHECK(v.kept);
        CHECK(v.looks == doctest::Approx(2.0));
        CHECK(v.find == doctest::Approx(2.0));

        s.score = 0.1, s.different = 0.05; // a little visible, and new functions
        g.new_functions = 3;
        v = judge(s, g, best);
        CHECK(v.kept);
        CHECK(v.acts == acts::NewFunctions);
        CHECK(v.find == doctest::Approx(1 + 0.1 / 0.6));

        g.silenced = 2; // past the bar of 1
        s.new_code = 41;
        v = judge(s, g, best);
        CHECK(v.acts == (acts::NewFunctions | acts::Silenced | acts::NewCode));
        CHECK(v.find == doctest::Approx(3 + 0.1 / 0.6));
        g.silenced = 1; // at the bar: not past it
        s.new_code = 40;
        CHECK(judge(s, g, best).acts == acts::NewFunctions);

        s.different = 0.01; // acts different, but nothing shows: not kept
        v = judge(s, g, best);
        CHECK(!v.kept);
        CHECK(v.acts == 0);

        s.alive = false; // stopped: never kept
        s.score = 0, s.different = 0.9;
        CHECK(!judge(s, g, best).kept);

        // A scene without play: the bars are 0, the score still needs the old floor.
        NormalBest none;
        none.runs = 36;
        RunScore t;
        t.alive = true;
        t.score = 0.2, t.different = 0.02;
        CHECK(!judge(t, {}, none).kept);
        t.score = 0.3;
        CHECK(judge(t, {}, none).kept);
        CHECK(judge(t, {}, none).looks == doctest::Approx(1.0));

        // An older exploration (no normal play): the old rule, the score against the floor.
        NormalBest old;
        t.score = 0.45;
        v = judge(t, g, old);
        CHECK(v.kept);
        CHECK(v.acts == 0);
        CHECK(v.find == doctest::Approx(1.5));
    }

    TEST_CASE("an exploration keeps normal play, each candidate's verdict and signals, and each try's find (#82)")
    {
        Exploration e;
        e.name = "Temple";
        e.normal.runs = 37;
        e.normal.score = 0.62;
        e.normal.new_functions = 1;
        e.normal.silenced = 0;
        e.normal.new_code = 12;
        Candidate c;
        c.family = Family::Swap;
        c.recipe.name = "Explored swap 4";
        c.score.score = 0.2;
        c.score.alive = true;
        c.measured.new_functions = 3;
        c.measured.silenced = 2;
        c.measured.apart = 0.25;
        c.measured.new_colours = 0.01;
        c.verdict = { 0.2 / 0.62, uint8_t(acts::NewFunctions | acts::Silenced), 2.3, true };
        e.candidates = { c };
        e.tried = { { Family::Swap, Tried::Visible, 0.2f, 2.3f }, { Family::Ram, Tried::Visible, 0.5f, 0.8f } };
        std::string error;
        auto back = decode_exploration(encode_exploration(e), error);
        REQUIRE_MESSAGE(back, error);
        CHECK(back->normal.runs == 37);
        CHECK(back->normal.score == doctest::Approx(0.62));
        CHECK(back->normal.new_functions == 1);
        CHECK(back->normal.new_code == 12);
        REQUIRE(back->candidates.size() == 1);
        auto &b = back->candidates[0];
        CHECK(b.verdict.kept);
        CHECK(b.verdict.acts == (acts::NewFunctions | acts::Silenced));
        CHECK(b.verdict.find == doctest::Approx(2.3));
        CHECK(b.verdict.looks == doctest::Approx(0.2 / 0.62));
        CHECK(b.measured.new_functions == 3);
        CHECK(b.measured.silenced == 2);
        CHECK(b.measured.apart == doctest::Approx(0.25));
        REQUIRE(back->tried.size() == 2);
        CHECK(back->tried[0].find == doctest::Approx(2.3f));
        CHECK(back->tried[1].find == doctest::Approx(0.8f));
        CHECK(acts_text(b.verdict.acts, b.measured, b.score).find("3 functions normal play never calls") !=
              std::string::npos);

        // The charts: the keep line at normal play's best, the finds by their strength.
        auto st = stats_of(e);
        CHECK(st.keep_line == doctest::Approx(0.62));
        CHECK(st.by_find);
        bool line_on_an_edge = false;
        for (auto &bin : st.bins)
        {
            CHECK(bin.kept_side == (bin.lo >= 0.62));
            line_on_an_edge |= bin.lo == doctest::Approx(0.62);
        }
        CHECK(line_on_an_edge);
        REQUIRE(st.finds.size() == 1); // 0.8 isn't a find
        CHECK(st.finds[0].at == 0);
        CHECK(st.finds[0].value == doctest::Approx(2.3f));
        CHECK(st.best.back().second == doctest::Approx(2.3f));

        auto html = gallery_html(e, [](const std::string &) { return std::nullopt; });
        CHECK(html.find("Normal play") != std::string::npos);
        CHECK(html.find("Acts different") != std::string::npos);
        CHECK(html.find("keep line 0.62") != std::string::npos);

        // An older file: no normal play, the old keep line, the finds by score.
        Exploration old;
        old.tried = { { Family::Ram, Tried::Visible, 0.5f } };
        auto os = stats_of(decode_exploration(encode_exploration(old), error).value());
        CHECK(!os.by_find);
        CHECK(os.keep_line == doctest::Approx(kMinScore));
        REQUIRE(os.finds.size() == 1);
        CHECK(os.finds[0].value == doctest::Approx(0.5f));
    }
}
