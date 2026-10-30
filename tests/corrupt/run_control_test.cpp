#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/run_control.h"

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
    bool refuse_loads = false;

    void tick()
    {
        auto &w = mem.of(Target::WRAM);
        uint8_t t = ++w[0];
        w[0x100 + t] = uint8_t(w[0x100 + t] * 3 + t);
    }

    std::vector<uint8_t> save_state() override
    {
        std::vector<uint8_t> out;
        for (auto &b : mem.bytes)
            out.insert(out.end(), b.begin(), b.end());
        return out;
    }

    bool load_state(const std::vector<uint8_t> &state) override
    {
        size_t total = 0;
        for (auto &b : mem.bytes)
            total += b.size();
        if (refuse_loads || state.size() != total)
            return false;
        size_t at = 0;
        for (auto &b : mem.bytes)
        {
            std::memcpy(b.data(), state.data() + at, b.size());
            at += b.size();
        }
        loads++;
        return true;
    }

    FrameSignals sample() override { return {}; }
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
