#include "doctest/doctest.h"
#include "corrupt/crash_learner.h"

using namespace corrupt;

namespace
{
struct Run
{
    std::deque<LoggedEvent> log;
    CrashLearner learner;
    CrashMap map;
    uint64_t frame = 0;

    void write(uint32_t offset, Target t = Target::WRAM) { log.push_back({ frame, 0, { t, offset, 0, 1 } }); }
    // Runs up to `to`, observing every frame like the session does.
    void until(uint64_t to)
    {
        while (frame < to)
        {
            frame++;
            learner.observe(log, frame, map);
        }
    }
    void step_write(uint32_t offset, Target t = Target::WRAM)
    {
        frame++;
        write(offset, t);
        learner.observe(log, frame, map);
    }
    // What the engine does on a rollback: its log loses the undone frames.
    void rollback_to(uint64_t restored)
    {
        while (!log.empty() && log.back().frame > restored)
            log.pop_back();
        frame = restored;
    }
};
} // namespace

TEST_SUITE("crash learner")
{
    TEST_CASE("a write survives once 900 frames pass without a crash")
    {
        Run r;
        r.step_write(0x100);
        r.until(900);
        CHECK(r.map.counts(Target::WRAM, 0x100).survivals == 0);
        r.until(901);
        CHECK(r.map.counts(Target::WRAM, 0x100).survivals == 1);
        CHECK(r.map.counts(Target::WRAM, 0x100).crashes == 0);
        CHECK(r.learner.pending() == 0);
    }

    TEST_CASE("a crash blames its window once per block, and forgets what the rollback undid")
    {
        Run r;
        r.step_write(0x500); // frame 1: before the window, stays pending
        r.until(100);
        for (int i = 0; i < 5; i++)
            r.step_write(0x1F00 + uint32_t(i)); // frames 101-105: one block, five writes
        r.step_write(0x0800, Target::VRAM);      // frame 106
        r.until(150);
        r.step_write(0x900);                     // frame 151: after the trouble started, before detection
        r.until(200);

        // The guard: trouble since frame 140, rolled back to the checkpoint at 60.
        r.learner.crash(60, 140, 60, r.map);
        r.rollback_to(60);
        CHECK(r.map.counts(Target::WRAM, 0x1F00).crashes == 1);
        CHECK(r.map.counts(Target::VRAM, 0x0800).crashes == 1);
        CHECK(r.map.counts(Target::WRAM, 0x900).crashes == 0); // outside the window
        CHECK(r.map.counts(Target::WRAM, 0x500).crashes == 0); // before it

        r.until(2000);
        CHECK(r.map.counts(Target::WRAM, 0x500).survivals == 1); // it really did survive
        CHECK(r.map.counts(Target::WRAM, 0x1F00).survivals == 0); // undone, never counted again
        CHECK(r.map.counts(Target::WRAM, 0x900).survivals == 0);
    }

    TEST_CASE("survivals count at most once per block every 300 frames")
    {
        Run r;
        for (int i = 0; i < 600; i++) // the same block every frame for 10 s
            r.step_write(0x40 + uint32_t(i % 16));
        r.until(3000);
        CHECK(r.map.counts(Target::WRAM, 0x40).survivals == 2);
    }

    TEST_CASE("observing twice adds nothing twice")
    {
        Run r;
        r.step_write(0x10);
        r.learner.observe(r.log, r.frame, r.map);
        r.learner.observe(r.log, r.frame, r.map);
        CHECK(r.learner.pending() == 1);
    }

    TEST_CASE("a rewind or restart without a crash drops what was undone")
    {
        Run r;
        r.step_write(0x10);
        r.until(50);
        r.step_write(0x20); // frame 51
        r.rollback_to(30);  // e.g. a saved moment loaded, or a hand rollback
        r.learner.observe(r.log, r.frame, r.map);
        CHECK(r.learner.pending() == 1);
        r.rollback_to(0); // the run restarts
        r.learner.observe(r.log, r.frame, r.map);
        CHECK(r.learner.pending() == 0);
        r.until(2000);
        CHECK(r.map.empty());
    }

    TEST_CASE("a block that crashes again and again turns hot; a busy innocent one doesn't")
    {
        Run r;
        uint64_t start = 0;
        for (int crash = 0; crash < 3; crash++)
        {
            start = r.frame;
            r.step_write(0x1F80); // the culprit
            r.step_write(0x3000); // a bystander
            r.until(start + 60);
            r.learner.crash(start, start + 30, start, r.map);
            r.rollback_to(start);
            // Afterwards the bystander gets hit again, and nothing happens; nor do writes
            // all over the rest of memory, as in a real game.
            r.step_write(0x3000);
            for (uint32_t quiet = 0x4000; quiet < 0x4100; quiet += 16)
                r.step_write(quiet);
            r.until(r.frame + 1000);
        }
        CHECK(r.map.hot(Target::WRAM, 0x1F80));
        CHECK_FALSE(r.map.hot(Target::WRAM, 0x3000));
    }
}

TEST_CASE("only the last 180 frames before the trouble are blamed")
{
    Run r;
    r.step_write(0x100); // frame 1: written long before
    r.until(400);
    r.step_write(0x200); // frame 401
    r.until(500);
    r.learner.crash(0, 500, 0, r.map); // a deep rollback, the guard's 5th try
    CHECK(r.map.counts(Target::WRAM, 0x100).crashes == 0);
    CHECK(r.map.counts(Target::WRAM, 0x200).crashes == 1);
}
