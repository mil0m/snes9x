#include "doctest/doctest.h"

#include "corrupt/paint_watch.h"

using namespace corrupt;

namespace
{
FrameSignals healthy(uint64_t frame)
{
    FrameSignals s;
    s.picture_hash = frame * 2654435761u;
    s.input = (frame / 20) % 2 ? 0x0080 : 0;
    return s;
}
FrameSignals stopped(uint64_t frame)
{
    auto s = healthy(frame);
    s.cpu_halted = true;
    return s;
}
} // namespace

TEST_SUITE("paint watch")
{
    TEST_CASE("unarmed it says nothing")
    {
        PaintWatch w;
        CHECK_FALSE(w.armed());
        CHECK(w.observe(stopped(1)).result == PaintWatch::Result::Nothing);
    }

    TEST_CASE("a crash inside the window: crashed, with the restore point and which batch")
    {
        PaintWatch w;
        w.arm({ 1, 2, 3 }, 7);
        REQUIRE(w.armed());
        for (uint64_t f = 0; f < 30; f++)
            CHECK(w.observe(healthy(f)).result == PaintWatch::Result::Nothing);
        auto out = w.observe(stopped(30));
        CHECK(out.result == PaintWatch::Result::Crashed);
        CHECK(out.entry == 7);
        REQUIRE(out.verdict);
        CHECK(out.verdict->signal == CrashSignal::CpuStopped);
        CHECK(out.state == std::vector<uint8_t>{ 1, 2, 3 });
        CHECK_FALSE(w.armed()); // once
    }

    TEST_CASE("quiet for the whole window: kept, and it stops watching")
    {
        PaintWatch w;
        w.arm({ 9 }, 2);
        PaintWatch::Outcome out;
        uint64_t f = 0;
        for (; f < PaintWatch::kFrames; f++)
        {
            out = w.observe(healthy(f));
            if (out.result != PaintWatch::Result::Nothing)
                break;
        }
        CHECK(out.result == PaintWatch::Result::Kept);
        CHECK(out.entry == 2);
        CHECK(f == PaintWatch::kFrames - 1);
        CHECK_FALSE(w.armed());
        CHECK(w.observe(stopped(f + 1)).result == PaintWatch::Result::Nothing);
    }

    TEST_CASE("a second Apply inside the window starts the watch over for the newer batch")
    {
        PaintWatch w;
        w.arm({ 1 }, 3);
        for (uint64_t f = 0; f < 500; f++)
            w.observe(healthy(f));
        w.arm({ 2 }, 4);
        for (uint64_t f = 500; f < 900; f++)
            CHECK(w.observe(healthy(f)).result == PaintWatch::Result::Nothing); // 400 frames: not yet
        auto out = w.observe(stopped(900));
        CHECK(out.result == PaintWatch::Result::Crashed);
        CHECK(out.entry == 4);
        CHECK(out.state == std::vector<uint8_t>{ 2 });
    }

    TEST_CASE("disarm forgets it (the guard is off, a game was closed)")
    {
        PaintWatch w;
        w.arm({ 1 }, 0);
        w.disarm();
        CHECK(w.observe(stopped(0)).result == PaintWatch::Result::Nothing);
    }
}
