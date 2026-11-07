#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/library.h"
#include "corrupt/moment.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <random>
#include <unistd.h>

using namespace corrupt;

namespace
{

// A game whose every frame depends on the pads, on its ROM, and on what corruption wrote
// before the frame and in its hook (part of the frame runs before the hook, part after), so
// a write in the wrong place or a missing pad shows at the end.
struct Game : CoreAccess
{
    FakeMemory &mem;
    Pads held{};
    uint64_t loads = 0;
    uint32_t acc = 1;
    std::function<void()> hook;
    int unseen = 0, unseen_drawn = 0;
    bool reads_drawing = false; // like a game reading the sprite overflow flags mid-frame (#86)
    bool drawing = true;        // the live game draws its frames

    explicit Game(FakeMemory &m) : mem(m) {}

    std::vector<uint8_t> &wram() { return mem.of(Target::WRAM); }
    uint8_t rom(uint32_t o) { return mem.of(Target::ROM)[o]; }

    void frame_once()
    {
        auto &w = wram();
        acc = acc * 31 + held[0] + held[1] * 7 + w[4] + rom(0x10) + (reads_drawing && !drawing);
        w[5] = uint8_t(acc);
        if (hook)
            hook();
        w[6] ^= uint8_t(w[4] ^ (acc >> 8));
        w[7] = uint8_t(w[7] + rom(0x11) + w[8]);
        w[0]++;
        mem.of(Target::VRAM)[w[0]] = uint8_t(acc >> 16);
    }
    std::vector<uint8_t> save_state() override
    {
        std::vector<uint8_t> s = wram();
        auto &v = mem.of(Target::VRAM);
        s.insert(s.end(), v.begin(), v.end());
        for (int i = 0; i < 4; i++)
            s.push_back(uint8_t(acc >> (8 * i)));
        return s;
    }
    bool load_state(const std::vector<uint8_t> &s) override
    {
        auto &w = wram();
        auto &v = mem.of(Target::VRAM);
        if (s.size() != w.size() + v.size() + 4)
            return false;
        std::copy(s.begin(), s.begin() + w.size(), w.begin());
        std::copy(s.begin() + w.size(), s.begin() + w.size() + v.size(), v.begin());
        acc = 0;
        for (int i = 0; i < 4; i++)
            acc |= uint32_t(s[w.size() + v.size() + i]) << (8 * i);
        loads++;
        return true;
    }
    FrameSignals sample() override { return {}; }
    void reset_console() override { loads++; }
    void run_unseen_frame(bool draw) override
    {
        unseen++;
        unseen_drawn += draw;
        drawing = draw;
        frame_once();
        drawing = true;
    }
    Pads pads() override { return held; }
    void set_pads(const Pads &p) override { held = p; }
    uint64_t timeline() const override { return loads; }
};

// The session around it: corruption writes through the recorder, the pads change as a
// player's would, and the frame hook writes noise (or plays a moment back).
struct Rig
{
    FakeMemory mem;
    MomentRecorder rec;
    RecordedMemory recorded{ mem, rec };
    Game game{ mem };
    MomentPlayer player;
    std::vector<uint8_t> clean;
    std::mt19937 rng{ 7 };
    uint64_t frame = 0;
    bool noise = true;

    Rig()
    {
        auto &r = mem.of(Target::ROM);
        for (size_t i = 0; i < r.size(); i++)
            r[i] = uint8_t(i * 13);
        clean = r;
        game.hook = [this] {
            if (player.playing())
                player.in_hook(mem);
            else if (noise)
            {
                rec.hook_begin();
                recorded.write(Target::WRAM, 4, uint8_t(rng()));
                if (frame % 50 == 0)
                    recorded.write(Target::ROM, 0x10 + (rng() & 1), uint8_t(rng()));
                rec.hook_end();
            }
        };
    }
    // One frame as the app plays it: input between frames, then before_frame, then the frame.
    void step()
    {
        if (frame % 23 == 0)
            game.held[0] = uint16_t(rng());
        if (frame % 61 == 0)
            game.held[1] = uint16_t(rng());
        if (frame % 37 == 0 && noise)
            recorded.write(Target::WRAM, 8, uint8_t(rng())); // painted between frames
        rec.before_frame(game, mem, clean, game.timeline());
        game.frame_once();
        frame++;
    }
    void play(int n)
    {
        for (int i = 0; i < n; i++)
            step();
    }
    MomentCheck check(const Moment &m) { return check_moment(m, player, game, mem, clean); }
};

// The crash guard's part between frames (#79): a mark every frame, after any writes made
// before it there; a checkpoint is the game then, and the ROM (the state doesn't hold it).
struct Checkpoint
{
    uint64_t frame = 0;
    std::vector<uint8_t> state, rom;
};

void play_marked(Rig &rig, int n)
{
    for (int i = 0; i < n; i++)
    {
        rig.step();
        if (rig.frame % 29 == 0)
            rig.recorded.write(Target::WRAM, 8, uint8_t(rig.rng())); // before the guard's turn
        rig.rec.mark(rig.frame);
    }
}

Checkpoint checkpoint(Rig &rig)
{
    return { rig.frame, rig.game.save_state(), rig.mem.of(Target::ROM) };
}

// Back to the checkpoint, as the guard does: the state loaded, and the ROM bytes that differ
// from then put back through the recorder (all of them, or only those at `undo`).
bool roll_back(Rig &rig, const Checkpoint &cp, std::vector<uint32_t> undo = {})
{
    rig.game.load_state(cp.state);
    bool kept = rig.rec.back_to(cp.frame, rig.game.timeline(), "the crash guard rolled back");
    auto &r = rig.mem.of(Target::ROM);
    for (uint32_t i = 0; i < r.size(); i++)
        if (r[i] != cp.rom[i] && (undo.empty() || std::find(undo.begin(), undo.end(), i) != undo.end()))
            rig.recorded.write(Target::ROM, i, cp.rom[i]);
    rig.frame = cp.frame;
    return kept;
}

} // namespace

TEST_SUITE("keep a golden moment")
{
    TEST_CASE("a kept moment plays back to exactly the game now, and the check puts everything back")
    {
        Rig rig;
        rig.play(1500);
        auto m = rig.rec.take();
        REQUIRE(m);
        // Keyframes at 0, 600 and 1200; the newest at least 10 s back is 600.
        CHECK(m->frames.size() == 900);
        CHECK(m->starts.empty());
        CHECK(!m->rom.empty()); // the hook changed ROM bytes before the keyframe
        auto wram = rig.mem.of(Target::WRAM);
        auto rom = rig.mem.of(Target::ROM);
        auto pads = rig.game.held;
        uint32_t acc = rig.game.acc;
        auto c = rig.check(*m);
        CHECK(c.played);
        CHECK(c.exact);
        CHECK(c.frames == 900);
        CHECK(c.text == "It plays back exactly (15.0 s).");
        CHECK(rig.game.unseen == 900);
        CHECK(rig.mem.of(Target::WRAM) == wram);
        CHECK(rig.mem.of(Target::ROM) == rom);
        CHECK(rig.game.held == pads);
        CHECK(rig.game.acc == acc);
        CHECK(!rig.player.playing());
        // And it goes on as if nothing happened.
        rig.play(10);
        CHECK(rig.rec.take());
    }

    TEST_CASE("the check notices a moment that doesn't match: writes in the wrong place, the pads missing")
    {
        Rig rig;
        rig.play(1300);
        auto m = rig.rec.take();
        REQUIRE(m);
        auto early = *m; // the hook's writes made before the frame instead
        for (auto &f : early.frames)
        {
            f.before.insert(f.before.end(), f.during.begin(), f.during.end());
            f.during.clear();
        }
        auto c = rig.check(early);
        CHECK(!c.exact);
        CHECK(c.played);
        CHECK(c.text.find("doesn't play back the same") != std::string::npos);
        CHECK(c.text.find("wram") != std::string::npos);
        auto no_pads = *m;
        for (auto &f : no_pads.frames)
            f.pads = {};
        CHECK(!rig.check(no_pads).exact);
        CHECK(rig.check(*m).exact);
    }

    TEST_CASE("a load nobody explained cuts the history: the moment starts after it")
    {
        Rig rig;
        rig.play(900);
        auto back = rig.game.save_state();
        rig.play(100);
        rig.game.load_state(back); // a rollback
        rig.play(300);
        auto m = rig.rec.take();
        REQUIRE(m);
        CHECK(m->frames.size() == 300);
        CHECK(m->starts == "the game jumped to another moment");
        CHECK(rig.check(*m).exact);
        // Said why: that's what it says.
        rig.rec.cut("the crash guard rolled back");
        CHECK(!rig.rec.take()); // nothing until the next frame
        rig.game.load_state(back);
        rig.play(5);
        m = rig.rec.take();
        REQUIRE(m);
        CHECK(m->starts == "the crash guard rolled back");
        CHECK(m->frames.size() == 5);
        CHECK(rig.check(*m).exact);
    }

    TEST_CASE("a run ahead that puts everything back isn't a cut")
    {
        Rig rig;
        rig.play(700);
        rig.rec.hold(true);
        auto state = rig.game.save_state();
        auto rom = rig.mem.of(Target::ROM);
        for (int i = 0; i < 60; i++)
            rig.step(); // its writes go nowhere
        rig.game.load_state(state);
        rig.mem.of(Target::ROM) = rom;
        rig.frame -= 60;
        rig.rec.hold(false);
        rig.rec.accept(rig.game.timeline());
        rig.play(700);
        auto m = rig.rec.take();
        REQUIRE(m);
        CHECK(m->frames.size() == 800); // from the keyframe at 600
        CHECK(m->starts.empty());
        CHECK(rig.check(*m).exact);
    }

    TEST_CASE("the instant revert's frame played again is recorded again, its ROM writes kept")
    {
        Rig rig;
        rig.play(649); // the next frame (649 % 50 != 0)... make it write ROM in its hook
        rig.game.held[0] = 5;
        rig.rec.before_frame(rig.game, rig.mem, rig.clean, rig.game.timeline());
        auto start = rig.game.save_state(); // the instant revert's frame-start checkpoint
        auto old = rig.game.hook;
        rig.game.hook = [&rig, old] {
            old();
            rig.rec.hook_begin();
            rig.recorded.write(Target::ROM, 0x11, 0x99); // a ROM write in the frame
            rig.rec.hook_end();
        };
        rig.game.frame_once();
        rig.frame++;
        // Caught: back to the frame's start, a byte fixed, the frame played again.
        rig.game.load_state(start);
        rig.recorded.write(Target::ROM, 0x10, 0x42);
        rig.rec.redo_frame(rig.game.timeline());
        rig.game.hook = [&rig] {
            if (rig.player.playing())
                return rig.player.in_hook(rig.mem);
            rig.rec.hook_begin();
            rig.recorded.write(Target::WRAM, 4, 3);
            rig.rec.hook_end();
        };
        rig.frame--;
        rig.step();
        rig.play(20);
        auto m = rig.rec.take();
        REQUIRE(m);
        CHECK(m->starts.empty());
        CHECK(m->frames.size() == 670); // nothing lost, nothing doubled
        auto c = rig.check(*m);
        CHECK_MESSAGE(c.exact, c.text);
    }

    TEST_CASE("a crash-guard rollback keeps the history before its checkpoint (#79)")
    {
        Rig rig;
        play_marked(rig, 783); // 783: a write before the guard's turn, which the checkpoint holds
        auto cp = checkpoint(rig);
        // After the guard's turn, before the next frame: lost to the load (WRAM), or not (ROM).
        rig.recorded.write(Target::WRAM, 8, 0x77);
        rig.recorded.write(Target::ROM, 0x11, 0x55);
        play_marked(rig, 100); // ROM writes in the hooks at 800 and 850
        CHECK(roll_back(rig, cp, { 0x10 })); // only 0x10 goes back: 0x11 stands, as RomKeeper leaves edits of the checkpoint's own frame
        play_marked(rig, 400);
        auto m = rig.rec.take();
        REQUIRE(m);
        CHECK(m->frames.size() == 1183); // from the keyframe at 0: nothing cut
        CHECK(m->starts.empty());
        auto c = rig.check(*m);
        CHECK_MESSAGE(c.exact, c.text);
        rig.rec.accept(rig.game.timeline()); // the check's loads put everything back
        // Every ROM byte back too.
        auto cp2 = checkpoint(rig);
        play_marked(rig, 60);
        CHECK(roll_back(rig, cp2));
        play_marked(rig, 10);
        m = rig.rec.take();
        REQUIRE(m);
        CHECK(m->frames.size() == 1193); // the keyframe at 1200 is gone with the timeline it was of
        c = rig.check(*m);
        CHECK_MESSAGE(c.exact, c.text);
    }

    TEST_CASE("a rollback past a keyframe drops it; one past every keyframe, or to a mark that's gone, is a cut (#79)")
    {
        Rig rig;
        play_marked(rig, 590);
        auto cp = checkpoint(rig);
        play_marked(rig, 120); // a keyframe at 600, of the timeline that crashed
        CHECK(roll_back(rig, cp));
        play_marked(rig, 100);
        auto m = rig.rec.take();
        REQUIRE(m);
        CHECK(m->frames.size() == 690); // from 0; the keyframe at 600 was taken again
        CHECK(rig.check(*m).exact);

        // A mark older than the history: a cut.
        Rig old;
        play_marked(old, 10);
        auto gone = checkpoint(old);
        play_marked(old, 2500); // the history starts at 1200 now
        CHECK(!roll_back(old, gone));
        CHECK(!old.rec.take());
        play_marked(old, 5);
        m = old.rec.take();
        REQUIRE(m);
        CHECK(m->frames.size() == 5);
        CHECK(m->starts == "the crash guard rolled back");
        CHECK(old.check(*m).exact);

        // Back to before the first keyframe (a mark nothing was recorded for): a cut.
        Rig early;
        early.rec.mark(0);
        auto start = checkpoint(early);
        play_marked(early, 50);
        CHECK(!roll_back(early, start));
        play_marked(early, 5);
        CHECK(early.rec.take()->frames.size() == 5);
    }

    TEST_CASE("a frame number marked again (the core stuck mid-frame) goes back to the newest mark (#79)")
    {
        Rig rig;
        play_marked(rig, 700);
        // The engine's frame stands still for three frames: the guard's newest checkpoint of it wins.
        Checkpoint cp;
        for (int i = 0; i < 3; i++)
        {
            rig.step();
            rig.frame = 700;
            rig.rec.mark(700);
            cp = checkpoint(rig);
        }
        play_marked(rig, 50);
        CHECK(roll_back(rig, cp));
        play_marked(rig, 30);
        auto m = rig.rec.take();
        REQUIRE(m);
        CHECK(m->frames.size() == 703 + 30);
        auto c = rig.check(*m);
        CHECK_MESSAGE(c.exact, c.text);
    }

    TEST_CASE("only the last keyframes are kept")
    {
        Rig rig;
        rig.play(5000);
        CHECK(rig.rec.frames_recorded() == 5000 - 3600);
        auto m = rig.rec.take();
        REQUIRE(m);
        CHECK(m->frames.size() == 800); // keyframes at 3600, 4200 and 4800: 4200 is the newest 10 s back
        CHECK(rig.check(*m).exact);
    }

    TEST_CASE("playing it: the ROM goes back to the moment's, a button pressed takes over")
    {
        Rig rig;
        rig.play(700);
        auto m = rig.rec.take();
        REQUIRE(m);
        rig.mem.of(Target::ROM)[0x500] ^= 0xFF; // changed after the moment: goes back to clean
        rig.noise = false;
        REQUIRE(rig.player.start(*m, rig.game, rig.mem, rig.clean));
        CHECK(rig.mem.of(Target::ROM)[0x500] == rig.clean[0x500]);
        for (int i = 0; i < 10; i++)
        {
            REQUIRE(rig.player.before_frame(rig.game, rig.mem));
            CHECK(!rig.player.taken_over(rig.game));
            rig.game.frame_once();
        }
        CHECK(rig.player.frame() == 10);
        rig.game.held[0] ^= 0x80; // pressed
        CHECK(rig.player.taken_over(rig.game));
        rig.player.stop();
        CHECK(!rig.player.playing());
        CHECK(!rig.player.before_frame(rig.game, rig.mem));
    }

    TEST_CASE("the file keeps everything, and a damaged one is refused")
    {
        Rig rig;
        rig.play(800);
        auto m = rig.rec.take();
        REQUIRE(m);
        m->name = "the HUD eats Max";
        m->game = { "GOOF TROOP", "abc123", "0badf00d" };
        m->recipe = R"({"name":"wram noise"})";
        m->seed = 0xFFFFFFFFFFFFFFF1ull;
        m->made_by = "mikael";
        m->made_at = "2026-11-04 21:13";
        m->starts = "the crash guard rolled back";
        auto bytes = encode_moment(*m);
        CHECK(bytes.rfind("S9XMOMENT\n", 0) == 0);
        std::string error;
        auto back = decode_moment(bytes, error);
        REQUIRE(back);
        CHECK(error.empty());
        CHECK(back->name == m->name);
        CHECK(back->game.title == "GOOF TROOP");
        CHECK(back->game.sha256 == "abc123");
        CHECK(back->recipe == m->recipe);
        CHECK(back->seed == m->seed);
        CHECK(back->made_by == "mikael");
        CHECK(back->made_at == m->made_at);
        CHECK(back->starts == m->starts);
        CHECK(back->state == m->state);
        CHECK(back->rom == m->rom);
        REQUIRE(back->frames.size() == m->frames.size());
        for (size_t i = 0; i < m->frames.size(); i++)
        {
            CHECK(back->frames[i].pads == m->frames[i].pads);
            CHECK(back->frames[i].before == m->frames[i].before);
            CHECK(back->frames[i].during == m->frames[i].during);
        }
        CHECK(back->after == m->after);
        CHECK(rig.check(*back).exact);

        CHECK(!decode_moment(bytes.substr(0, bytes.size() - 3), error));
        CHECK(error == "it's cut short or damaged");
        CHECK(!decode_moment("hello", error));
        CHECK(error == "not a kept moment (.s9xmoment)");
        auto newer = bytes;
        newer[10] = 9;
        CHECK(!decode_moment(newer, error));
        CHECK(error.find("newer") != std::string::npos);
    }

    TEST_CASE("the file is packed: a save state is mostly zeros")
    {
        Rig rig;
        rig.play(700);
        auto m = rig.rec.take();
        REQUIRE(m);
        auto bytes = encode_moment(*m);
#ifdef ZLIB
        CHECK(bytes.size() < m->state.size() / 4);
#endif
        std::string error;
        auto back = decode_moment(bytes, error);
        REQUIRE(back);
        CHECK(back->state == m->state);
        CHECK(rig.check(*back).exact);
        auto damaged = bytes;
        damaged[damaged.size() / 2] ^= 0x5A;
        CHECK(!decode_moment(damaged, error));
    }

    TEST_CASE("the library keeps moments in the game's folder: listed newest first, renamed, deleted")
    {
        namespace fs = std::filesystem;
        auto root = fs::temp_directory_path() / ("moments-test-" + std::to_string(::getpid()));
        fs::remove_all(root);
        Library lib(root);
        GameId game{ "GOOF TROOP", "abcdef0123456789", "" };
        Rig rig;
        rig.play(650);
        auto m = rig.rec.take();
        REQUIRE(m);
        m->game = game;
        m->name = "The HUD eats Max";
        m->recipe_name = "Gentle WRAM noise";
        m->made_by = "mikael";
        m->made_at = "2026-11-04 21:13:05";
        auto first = lib.save_moment(game, *m);
        REQUIRE(first.error.empty());
        CHECK(first.path == lib.moments_dir(game) / "the-hud-eats-max.s9xmoment");
        m->made_at = "2026-11-04 21:20:00";
        auto second = lib.save_moment(game, *m); // the same name: a free one
        REQUIRE(second.error.empty());
        CHECK(second.path.filename() == "the-hud-eats-max-2.s9xmoment");
        { std::ofstream(lib.moments_dir(game) / "junk.s9xmoment") << "hello"; }

        std::vector<std::string> errors;
        auto list = lib.list_moments(game, &errors);
        REQUIRE(list.size() == 2);
        CHECK(list[0].path == second.path); // newest first
        CHECK(list[0].name == "The HUD eats Max");
        CHECK(list[0].recipe_name == "Gentle WRAM noise");
        CHECK(list[0].made_by == "mikael");
        CHECK(list[0].seconds == doctest::Approx(m->seconds()).epsilon(0.01));
        CHECK(list[0].game.sha256 == game.sha256);
        REQUIRE(errors.size() == 1);
        CHECK(errors[0].find("junk.s9xmoment") != std::string::npos);

        auto renamed = lib.rename_moment(first.path, "Max walks on the ceiling");
        REQUIRE(renamed.error.empty());
        CHECK(renamed.path.filename() == "max-walks-on-the-ceiling.s9xmoment");
        CHECK(!fs::exists(first.path));
        std::string error;
        auto info = read_moment_info(renamed.path, error);
        REQUIRE(info);
        CHECK(info->name == "Max walks on the ceiling");
        CHECK(lib.delete_moment(renamed.path).empty());
        CHECK(lib.list_moments(game).size() == 1);
        CHECK(!lib.delete_moment(renamed.path).empty());
        fs::remove_all(root);
    }
}

TEST_CASE("a moment of a game that reads what drawing sets is checked drawn when not drawing drifts (#86)")
{
    Rig r;
    r.game.reads_drawing = true;
    r.play(1500);
    auto m = r.rec.take();
    REQUIRE(m);
    auto c = r.check(*m);
    CAPTURE(c.text);
    CHECK(c.exact);
    CHECK(c.drawn);
    CHECK(r.game.unseen_drawn == int(m->frames.size())); // once not drawn, then once drawn
    CHECK(r.game.unseen == 2 * int(m->frames.size()));
    // Not drawn only: it drifts.
    CheckHooks plain;
    plain.redraw = false;
    auto p = check_moment(*m, r.player, r.game, r.mem, r.clean, plain);
    CHECK(p.played);
    CHECK(!p.exact);
    CHECK(!p.drawn);
    // A game that doesn't: one playback, not drawn, as before.
    Rig q;
    q.play(1500);
    auto n = q.rec.take();
    REQUIRE(n);
    auto d = q.check(*n);
    CHECK(d.exact);
    CHECK(!d.drawn);
    CHECK(q.game.unseen == int(n->frames.size()));
    CHECK(q.game.unseen_drawn == 0);
}
