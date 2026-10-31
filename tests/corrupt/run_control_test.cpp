#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/rom_keeper.h"
#include "corrupt/run_control.h"

#include <algorithm>
#include <cstring>

using namespace corrupt;

namespace
{

// A pretend game: its whole state is FakeMemory, and every frame it moves on
// (a frame counter in WRAM and some bytes that depend on it), so states differ.
struct FakeGame : CoreAccess
{
    FakeMemory mem;
    int loads = 0;
    int resets = 0;
    bool refuse_loads = false;

    void tick()
    {
        auto &w = mem.of(Target::WRAM);
        uint8_t t = ++w[0];
        w[0x100 + t] = uint8_t(w[0x100 + t] * 3 + t);
    }

    std::vector<uint8_t> save_state() override
    {
        std::vector<uint8_t> out; // like a real save state, without the ROM
        for (auto t : kAllTargets)
            if (t != Target::ROM)
                out.insert(out.end(), mem.of(t).begin(), mem.of(t).end());
        return out;
    }

    bool load_state(const std::vector<uint8_t> &state) override
    {
        size_t total = 0;
        for (auto t : kAllTargets)
            if (t != Target::ROM)
                total += mem.of(t).size();
        if (refuse_loads || state.size() != total)
            return false;
        size_t at = 0;
        for (auto t : kAllTargets)
            if (t != Target::ROM)
            {
                auto &b = mem.of(t);
                std::memcpy(b.data(), state.data() + at, b.size());
                at += b.size();
            }
        loads++;
        return true;
    }

    FrameSignals sample() override { return {}; }

    void reset_console() override // a power cycle: RAM to 0x55, like S9xReset
    {
        resets++;
        std::fill(mem.of(Target::WRAM).begin(), mem.of(Target::WRAM).end(), 0x55);
    }
};

Recipe noise()
{
    Rule r;
    r.name = "noise";
    r.range = { Target::WRAM, 0x1000, 0x1FFF };
    r.op = { .type = OpType::Random, .min = 0, .max = 255 };
    r.when = { .type = TriggerType::ChancePerFrame, .chance = 0.3 };
    r.count = 3;
    Recipe recipe;
    recipe.seed = 31337;
    recipe.rules = { r };
    return recipe;
}

struct Rig
{
    FakeGame game;
    Engine engine;
    RunControl control;
    std::vector<RunControl::Started> started;

    Rig()
    {
        engine.hold_starts(true);
        engine.set_recipe(noise());
    }

    // Like the front end: the engine runs inside the frame, the control after it.
    void frames(int n)
    {
        for (int i = 0; i < n; i++)
        {
            engine.on_frame(game.mem);
            game.tick();
            auto s = control.between_frames(engine, game);
            if (s != RunControl::Started::Nothing)
                started.push_back(s);
        }
    }
};

} // namespace

TEST_CASE("a start snapshots the state at the boundary, before the first corrupted frame")
{
    Rig rig;
    rig.frames(30);
    CHECK_FALSE(rig.control.has_anchor());
    rig.engine.set_active(true);
    rig.frames(1);
    REQUIRE(rig.started == std::vector{ RunControl::Started::Fresh });
    CHECK(rig.control.has_anchor());
    CHECK_FALSE(rig.control.anchor_is_from_saved());
    CHECK(rig.control.anchor() == rig.game.save_state()); // nothing ran since
    CHECK(rig.engine.current_frame() == 0);
    CHECK_FALSE(rig.engine.waiting());
    rig.frames(1);
    CHECK(rig.engine.current_frame() == 1);
}

TEST_CASE("starting from the saved moment replays the run byte for byte")
{
    Rig rig;
    rig.frames(30);
    rig.engine.set_active(true);
    rig.frames(1);
    auto anchor = rig.control.anchor();
    rig.frames(200);
    auto original = rig.game.mem.bytes;

    rig.frames(77); // play on somewhere else
    rig.control.request_start_from(anchor);
    rig.engine.reroll(noise().seed); // the panel's Load restarts the run
    rig.frames(1);
    CHECK(rig.started.back() == RunControl::Started::FromAnchor);
    CHECK(rig.control.anchor_is_from_saved());
    CHECK(rig.game.loads == 1);
    rig.frames(200);
    CHECK(rig.game.mem.bytes == original);
}

TEST_CASE("a saved moment waits for the next start, and is used once")
{
    Rig rig;
    rig.frames(10);
    auto early = rig.game.save_state();
    rig.frames(10);
    rig.control.request_start_from(early);
    rig.frames(5); // not active: nothing to start
    CHECK(rig.game.loads == 0);
    rig.engine.set_active(true);
    rig.frames(1);
    CHECK(rig.game.loads == 1);
    rig.engine.reroll(5);
    rig.frames(1);
    CHECK(rig.game.loads == 1);
    CHECK(rig.started.back() == RunControl::Started::Fresh);
    CHECK_FALSE(rig.control.anchor_is_from_saved());
}

TEST_CASE("a state that won't load starts from the current moment instead, and says so")
{
    Rig rig;
    rig.frames(10);
    rig.control.request_start_from({ 1, 2, 3 });
    rig.engine.set_active(true);
    rig.frames(1);
    CHECK(rig.started.back() == RunControl::Started::AnchorFailed);
    CHECK_FALSE(rig.engine.waiting());
    CHECK(rig.control.anchor() == rig.game.save_state());
    CHECK_FALSE(rig.control.anchor_is_from_saved());
}

TEST_CASE("cancelling a requested moment starts fresh")
{
    Rig rig;
    rig.control.request_start_from(rig.game.save_state());
    rig.control.cancel_start_from();
    rig.engine.set_active(true);
    rig.frames(1);
    CHECK(rig.started.back() == RunControl::Started::Fresh);
    CHECK(rig.game.loads == 0);
}

TEST_CASE("forgetting the moment drops it until the next start")
{
    Rig rig;
    rig.engine.set_active(true);
    rig.frames(1);
    REQUIRE(rig.control.has_anchor());
    rig.control.request_start_from(rig.game.save_state());
    rig.control.forget_anchor();
    CHECK_FALSE(rig.control.has_anchor());
    CHECK_FALSE(rig.control.start_from_pending()); // a new recipe doesn't inherit a requested moment either
    rig.engine.reroll(3);
    rig.frames(1);
    CHECK(rig.control.has_anchor());
}

TEST_SUITE("ROM runs")
{
    Recipe classic(bool rom_at_load = true)
    {
        Rule rom;
        rom.name = "rom";
        rom.range = { Target::ROM, 0x8000, 0xFFFF };
        rom.op = { .type = OpType::Random, .min = 1, .max = 255 };
        rom.when = { .type = TriggerType::AtLoad };
        rom.count = 64;
        Rule ram = rom;
        ram.name = "ram";
        ram.range = { Target::WRAM, 0x2000, 0x20FF };
        ram.count = 16;
        Rule live = rom;
        live.name = "live rom";
        live.when = { .type = TriggerType::EveryFrames, .every = 10 };
        live.range = { Target::ROM, 0x10000, 0x1FFFF };
        Recipe r;
        r.seed = 77;
        r.rules = { ram, live };
        if (rom_at_load)
            r.rules.insert(r.rules.begin(), rom);
        return r;
    }

    struct RomRig
    {
        FakeGame game;
        RomKeeper rom{ game.mem };
        Engine engine;
        RunControl control;
        std::vector<RunControl::Started> started;

        explicit RomRig(Recipe recipe)
        {
            rom.set_clock([this] { return engine.current_frame(); });
            rom.game_loaded({});
            engine.hold_starts(true);
            engine.set_recipe(std::move(recipe));
        }
        void frames(int n)
        {
            for (int i = 0; i < n; i++)
            {
                engine.on_frame(rom);
                game.tick();
                auto s = control.between_frames(engine, game, &rom);
                if (s != RunControl::Started::Nothing)
                    started.push_back(s);
            }
        }
        bool wram_all(uint32_t first, uint32_t last, uint8_t v)
        {
            for (uint32_t a = first; a <= last; a++)
                if (game.mem.of(Target::WRAM)[a] != v)
                    return false;
            return true;
        }
    };

    TEST_CASE("a classic start: clean ROM, ROM edits, a power cycle, the anchor, then the RAM edits")
    {
        RomRig rig(classic());
        rig.rom.write(Target::ROM, 0x9000, 0xAB); // left over from an earlier run
        rig.frames(5);
        rig.engine.set_active(true);
        rig.frames(1);
        REQUIRE(rig.started == std::vector{ RunControl::Started::Fresh });
        CHECK(rig.game.resets == 1);
        CHECK(rig.rom.changed() > 32); // the at_load ROM rule; the leftover was undone first
        CHECK(rig.engine.current_frame() == 0);

        // The anchor is the freshly booted console: RAM all 0x55, before the RAM at_load rule.
        FakeGame anchor_view;
        REQUIRE(anchor_view.load_state(rig.control.anchor()));
        CHECK(std::all_of(anchor_view.mem.of(Target::WRAM).begin() + 0x2000,
                          anchor_view.mem.of(Target::WRAM).begin() + 0x2100, [](uint8_t b) { return b == 0x55; }));
        CHECK_FALSE(rig.wram_all(0x2000, 0x20FF, 0x55)); // the RAM rule wrote after the anchor
    }

    TEST_CASE("from the saved moment: no power cycle, and the same ROM and RAM as the first time")
    {
        RomRig rig(classic());
        rig.frames(5);
        rig.engine.set_active(true);
        rig.frames(1);
        auto anchor = rig.control.anchor();
        auto rom_then = rig.game.mem.of(Target::ROM);
        auto ram_then = rig.game.mem.of(Target::WRAM);

        rig.frames(100); // the live ROM rule keeps editing
        CHECK(rig.game.mem.of(Target::ROM) != rom_then);
        rig.control.request_start_from(anchor);
        rig.engine.reroll(classic().seed);
        rig.frames(1);
        CHECK(rig.started.back() == RunControl::Started::FromAnchor);
        CHECK(rig.game.resets == 1);
        CHECK(rig.game.mem.of(Target::ROM) == rom_then);
        CHECK(rig.game.mem.of(Target::WRAM) == ram_then);
    }

    TEST_CASE("a run without ROM at_load rules starts from the clean ROM, with no power cycle")
    {
        RomRig rig(classic(false));
        rig.engine.set_active(true);
        rig.frames(1);
        rig.frames(50);
        CHECK(rig.rom.changed() > 0); // the live rule
        rig.engine.reroll(5);
        rig.frames(1);
        CHECK(rig.game.resets == 0);
        CHECK(rig.rom.changed() == 0);
    }

    TEST_CASE("without a ROM keeper, at_load rules don't fire (older callers)")
    {
        Rig rig;
        rig.engine.set_recipe(classic());
        rig.engine.set_active(true);
        rig.frames(1);
        CHECK(rig.game.resets == 0);
        CHECK(rig.engine.event_log().empty());
    }
}
