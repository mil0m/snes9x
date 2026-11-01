#include "doctest/doctest.h"
#include "corrupt/crash_map_keeper.h"

using namespace corrupt;

namespace
{
struct Feed
{
    std::deque<LoggedEvent> log;
    CrashMapKeeper keeper;
    uint64_t frame = 0;

    void frames(uint64_t n, std::optional<uint32_t> write_each = std::nullopt)
    {
        for (uint64_t i = 0; i < n; i++)
        {
            frame++;
            if (write_each)
                log.push_back({ frame, 0, { Target::WRAM, *write_each, 0, 1 } });
            keeper.observe(log, frame);
        }
    }
    GuardOutcome crash_now()
    {
        GuardOutcome out;
        out.kind = GuardOutcome::RolledBack;
        out.blame_after = frame > 60 ? frame - 60 : 0;
        out.blame_through = frame;
        out.restored_frame = out.blame_after;
        while (!log.empty() && log.back().frame > out.restored_frame)
            log.pop_back();
        frame = out.restored_frame;
        return out;
    }
};
} // namespace

TEST_SUITE("crash map keeper")
{
    TEST_CASE("crashes make blocks hot, and the engine's mask follows")
    {
        Feed f;
        CHECK(f.keeper.hot_mask().empty());
        f.frames(10, 0x1F20);
        CHECK_FALSE(f.keeper.crashed(f.crash_now())); // one crash: nothing hot yet
        f.frames(10, 0x1F20);
        CHECK(f.keeper.crashed(f.crash_now())); // the second turns it hot: a new mask
        CHECK(f.keeper.hot_mask().is_hot(Target::WRAM, 0x1F2F));
        CHECK(f.keeper.hot_blocks() == 1);
        CHECK(f.keeper.combined().counts(Target::WRAM, 0x1F20).crashes == 2);
    }

    TEST_CASE("everyone else's counts count too, but only mine are saved")
    {
        Feed f;
        CrashMaps loaded;
        loaded.others.add_crash(Target::WRAM, 0x500, 1);
        loaded.mine.add_survival(Target::ARAM, 0x20, 4);
        f.keeper.reset(loaded);
        f.frames(10, 0x500);
        CHECK(f.keeper.crashed(f.crash_now())); // 1 of Erik's + 1 of mine
        CHECK(f.keeper.mine().counts(Target::WRAM, 0x500).crashes == 1);
        CHECK(f.keeper.mine().counts(Target::ARAM, 0x20).survivals == 4);
        CHECK(f.keeper.combined().counts(Target::WRAM, 0x500).crashes == 2);
    }

    TEST_CASE("a save is due after a crash, then every minute while counts change")
    {
        Feed f;
        f.frames(100);
        CHECK_FALSE(f.keeper.save_due()); // nothing learned yet
        f.frames(10, 0x40);
        f.keeper.crashed(f.crash_now());
        CHECK(f.keeper.save_due());
        f.keeper.saved();
        CHECK_FALSE(f.keeper.save_due());

        f.frames(1000, 0x80); // survivals start after 900 frames
        CHECK_FALSE(f.keeper.save_due()); // not a minute yet
        CHECK(f.keeper.unsaved());        // but closing the game would save
        f.frames(CrashMapKeeper::kSaveEveryFrames);
        CHECK(f.keeper.save_due());
        f.keeper.saved();
        f.frames(CrashMapKeeper::kSaveEveryFrames * 2);
        CHECK_FALSE(f.keeper.save_due()); // nothing changed
    }

    TEST_CASE("survivals can cool a block down, and the mask follows at the next save")
    {
        Feed f;
        CrashMaps loaded;
        loaded.mine.add_crash(Target::WRAM, 0x300, 2); // hot: 2 / 4
        f.keeper.reset(loaded);
        CHECK(f.keeper.hot_mask().is_hot(Target::WRAM, 0x300));
        for (int i = 0; i < 4; i++) // four survivals, spaced out
            f.frames(CrashLearner::kSurvivalSpacing, 0x300);
        f.frames(CrashLearner::kSurviveFrames + CrashMapKeeper::kSaveEveryFrames);
        REQUIRE(f.keeper.save_due());
        CHECK(f.keeper.saved()); // the hot set changed: the engine needs the new mask
        CHECK_FALSE(f.keeper.hot_mask().is_hot(Target::WRAM, 0x300));
    }

    TEST_CASE("danger per byte for the viewer")
    {
        Feed f;
        CrashMaps loaded;
        loaded.mine.add_crash(Target::WRAM, 0x10, 2);
        loaded.mine.add_crash(Target::WRAM, 0x40, 1);
        f.keeper.reset(loaded);
        auto d = f.keeper.danger(Target::WRAM, 0x100);
        REQUIRE(d.size() == 0x100);
        CHECK(d[0x10] == doctest::Approx(0.5));
        CHECK(d[0x1F] == doctest::Approx(0.5));
        CHECK(d[0x40] == doctest::Approx(1.0 / 3));
        CHECK(d[0x20] == 0.0f);
        CHECK(f.keeper.danger(Target::VRAM, 16).empty()); // nothing known there: no vector at all
    }

    TEST_CASE("counts and hot flags per block for the viewer, everyone's summed")
    {
        Feed f;
        CrashMaps loaded;
        loaded.mine.add_crash(Target::WRAM, 0x10, 2);
        loaded.others.add_crash(Target::WRAM, 0x12, 1); // Erik's, same block
        loaded.mine.add_crash(Target::WRAM, 0x40, 1);   // one crash is never hot
        loaded.mine.add_survival(Target::WRAM, 0x80, 20);
        f.keeper.reset(loaded);

        auto counts = f.keeper.counts_per_block(Target::WRAM, 0x95); // the last block is partial
        REQUIRE(counts.size() == 10);
        CHECK(counts[1].crashes == 3);
        CHECK(counts[4].crashes == 1);
        CHECK(counts[8].survivals == 20);
        CHECK(counts[0].crashes + counts[0].survivals == 0);

        auto hot = f.keeper.hot_per_block(Target::WRAM, 0x95);
        REQUIRE(hot.size() == 10);
        CHECK(hot[1] == 1);
        CHECK(hot[4] == 0);
        CHECK(hot[8] == 0);

        CHECK(f.keeper.counts_per_block(Target::WRAM, 0x20).size() == 2); // blocks past the end left out
        CHECK(f.keeper.hot_per_block(Target::VRAM, 32) == std::vector<uint8_t>(2, 0));
    }

    TEST_CASE("ROM blamed by use: blamed bytes are crashes for their block, survivors are survivals")
    {
        Feed f;
        f.keeper.learn_rom_by_use(true);
        f.frames(10);
        GuardOutcome out = f.crash_now();
        out.rom_blamed = { 0x1234, 0x1238, 0x2000 }; // two in one block
        f.keeper.crashed(out);
        CHECK(f.keeper.mine().counts(Target::ROM, 0x1230).crashes == 1);
        CHECK(f.keeper.mine().counts(Target::ROM, 0x2000).crashes == 1);
        f.keeper.rom_survived({ 0x5000, 0x5001 });
        CHECK(f.keeper.mine().counts(Target::ROM, 0x5000).survivals == 1);
        CHECK(f.keeper.unsaved());
    }

    TEST_CASE("bytes the instant revert caught are crashes for their block, and nothing else is blamed")
    {
        Feed f;
        f.frames(10);
        f.keeper.rom_crashed({ 0x1234, 0x1238 });
        CHECK(f.keeper.mine().counts(Target::ROM, 0x1230).crashes == 1);
        CHECK(f.keeper.mine().counts(Target::WRAM, 0).crashes == 0);
        CHECK(f.keeper.save_due());
        CHECK_FALSE(f.keeper.rom_crashed({}));
    }

    TEST_CASE("with ROM blamed by use, ROM writes in the log no longer count")
    {
        Feed f;
        f.keeper.learn_rom_by_use(true);
        for (int i = 0; i < 10; i++)
        {
            f.frame++;
            f.log.push_back({ f.frame, 0, { Target::ROM, 0x4000, 0, 1 } });
            f.log.push_back({ f.frame, 0, { Target::WRAM, 0x100, 0, 1 } });
            f.keeper.observe(f.log, f.frame);
        }
        f.keeper.crashed(f.crash_now());
        CHECK(f.keeper.mine().counts(Target::ROM, 0x4000).crashes == 0);
        CHECK(f.keeper.mine().counts(Target::WRAM, 0x100).crashes == 1);
    }
}
