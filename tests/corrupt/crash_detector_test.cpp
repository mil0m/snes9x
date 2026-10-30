#include "doctest/doctest.h"
#include "corrupt/crash_detector.h"

using namespace corrupt;

namespace
{
// A healthy, animated frame: everything fine, picture keeps changing.
FrameSignals healthy(uint64_t frame)
{
    FrameSignals s;
    s.picture_hash = frame * 2654435761u;
    return s;
}

// A player pressing buttons: input changes every 20 frames.
uint16_t mashing(uint64_t f)
{
    return (f / 20) % 2 ? 0x0080 : 0x0000;
}

// Feeds `n` frames made by `make` starting at `from`; returns the first verdict.
template <typename F>
std::optional<CrashVerdict> feed(CrashDetector &d, uint64_t from, uint64_t n, F make)
{
    for (uint64_t f = from; f < from + n; f++)
        if (auto v = d.observe(f, make(f)))
            return v;
    return std::nullopt;
}
} // namespace

TEST_CASE("a healthy game never trips it")
{
    CrashDetector d;
    CHECK_FALSE(feed(d, 1, 3000, healthy));
}

TEST_CASE("a stopped CPU is caught on the frame it happens")
{
    CrashDetector d;
    feed(d, 1, 100, healthy);
    auto v = d.observe(101, [] { auto s = healthy(101); s.cpu_halted = true; s.pc = 0x801234; return s; }());
    REQUIRE(v);
    CHECK(v->signal == CrashSignal::CpuStopped);
    CHECK(v->frame == 101);
    CHECK(v->since_frame == 101);
    CHECK_FALSE(v->heuristic);
    CHECK(v->text.find("$80:1234") != std::string::npos);
}

TEST_CASE("the core's deadlock stop is its own signal")
{
    CrashDetector d;
    auto s = healthy(5);
    s.cpu_halted = true;
    s.emulation_stopped = true;
    auto v = d.observe(5, s);
    REQUIRE(v);
    CHECK(v->signal == CrashSignal::Deadlock);
}

TEST_CASE("runaway PC needs a sustained run, and remembers where it started")
{
    CrashDetector d;
    auto bad = [](uint64_t f) { auto s = healthy(f); s.pc_class = PcClass::Io; s.pc = 0x004500; return s; };
    CHECK_FALSE(feed(d, 1, 29, bad));
    auto v = d.observe(30, bad(30));
    REQUIRE(v);
    CHECK(v->signal == CrashSignal::RunawayPc);
    CHECK(v->since_frame == 1);
    CHECK_FALSE(v->heuristic);
}

TEST_CASE("one good frame resets a runaway count")
{
    CrashDetector d;
    auto bad = [](uint64_t f) { auto s = healthy(f); s.pc_class = PcClass::OpenBus; return s; };
    feed(d, 1, 20, bad);
    d.observe(21, healthy(21));
    CHECK_FALSE(feed(d, 22, 29, bad));
}

TEST_CASE("NMI off for too long is a heuristic crash")
{
    CrashDetector d;
    auto off = [](uint64_t f) { auto s = healthy(f); s.nmi_enabled = false; return s; };
    feed(d, 1, 50, healthy);
    CHECK_FALSE(feed(d, 51, 299, off));
    auto v = d.observe(350, off(350));
    REQUIRE(v);
    CHECK(v->signal == CrashSignal::NmiOff);
    CHECK(v->since_frame == 51);
    CHECK(v->heuristic);
}

TEST_CASE("a frozen picture while the player presses buttons is a heuristic crash")
{
    CrashDetector d;
    auto frozen = [](uint64_t f) { FrameSignals s; s.picture_hash = 42; s.input = mashing(f); return s; };
    feed(d, 1, 10, healthy);
    auto v = feed(d, 11, 400, frozen);
    REQUIRE(v);
    CHECK(v->signal == CrashSignal::FrozenPicture);
    CHECK(v->since_frame == 11);
    CHECK(v->frame == 11 + 300);
    CHECK(v->heuristic);
}

TEST_CASE("a black screen uses its own, longer threshold")
{
    CrashDetector d;
    auto black = [](uint64_t f) { FrameSignals s; s.forced_blank = true; s.picture_hash = 0; s.input = mashing(f); return s; };
    auto v = feed(d, 1, 1000, black);
    REQUIRE(v);
    CHECK(v->signal == CrashSignal::BlackScreen);
    CHECK(v->frame == 480);

    CrashDetector dim;
    auto zero_brightness = [](uint64_t f) { FrameSignals s; s.brightness = 0; s.picture_hash = 0; s.input = mashing(f); return s; };
    auto v2 = feed(dim, 1, 1000, zero_brightness);
    REQUIRE(v2);
    CHECK(v2->signal == CrashSignal::BlackScreen);
}

TEST_CASE("signals can be switched off")
{
    DetectorConfig cfg;
    cfg.frozen_frames = 0;
    cfg.nmi_off_frames = 0;
    CrashDetector d(cfg);
    auto stuck = [](uint64_t) { FrameSignals s; s.picture_hash = 7; s.nmi_enabled = false; return s; };
    CHECK_FALSE(feed(d, 1, 2000, stuck));

    DetectorConfig all_off;
    all_off.enabled = false;
    CrashDetector off(all_off);
    auto s = healthy(1);
    s.cpu_halted = true;
    CHECK_FALSE(off.observe(1, s));
}

TEST_CASE("reset forgets everything, e.g. after a rollback")
{
    CrashDetector d;
    auto frozen = [](uint64_t f) { FrameSignals s; s.picture_hash = 42; s.input = mashing(f); return s; };
    feed(d, 1, 299, frozen);
    d.reset();
    CHECK_FALSE(feed(d, 300, 299, frozen));
}

TEST_CASE("after reporting, a still-broken game reports again only after a fresh run")
{
    CrashDetector d;
    auto frozen = [](uint64_t f) { FrameSignals s; s.picture_hash = 42; s.input = mashing(f); return s; };
    REQUIRE(feed(d, 1, 400, frozen));
    CHECK_FALSE(d.observe(302, frozen(302))); // no repeat on the very next frame
}

TEST_CASE("frames that weren't drawn (frameskip, fast-forward) never count as frozen")
{
    CrashDetector d;
    // Every other frame skipped: the screen buffer keeps the old picture on those.
    auto skipping = [](uint64_t f) {
        FrameSignals s;
        s.picture_valid = f % 2 == 0;
        s.picture_hash = s.picture_valid ? f : 12345; // stale hash on skipped frames
        return s;
    };
    CHECK_FALSE(feed(d, 1, 3000, skipping));

    // All frames skipped: no picture information at all, so no verdict either.
    CrashDetector blind;
    auto none = [](uint64_t) { FrameSignals s; s.picture_valid = false; s.picture_hash = 7; return s; };
    CHECK_FALSE(feed(blind, 1, 3000, none));
}

TEST_CASE("a still screen with nobody pressing anything is not a crash")
{
    // Reading a text box, sitting in a menu, a pause screen.
    CrashDetector d;
    auto calm = [](uint64_t) { FrameSignals s; s.picture_hash = 42; return s; };
    CHECK_FALSE(feed(d, 1, 5000, calm));
    CrashDetector dark;
    auto black = [](uint64_t) { FrameSignals s; s.forced_blank = true; return s; };
    CHECK_FALSE(feed(dark, 1, 5000, black));
}

TEST_CASE("a single press is not enough evidence")
{
    CrashDetector d;
    auto one_press = [](uint64_t f) { FrameSignals s; s.picture_hash = 42; s.input = f > 100 ? 0x0080 : 0; return s; };
    CHECK_FALSE(feed(d, 1, 2000, one_press));
}

TEST_CASE("an STP is named as such even when the core also calls it a deadlock")
{
    CrashDetector d;
    auto s = healthy(5);
    s.cpu_halted = true;
    s.emulation_stopped = true; // STP never adds cycles, so the core's watchdog fires too
    s.opcode = 0xDB;
    s.pc = 0x008018;
    auto v = d.observe(5, s);
    REQUIRE(v);
    CHECK(v->signal == CrashSignal::CpuStopped);
    CHECK(v->text.find("STP at $00:8018") != std::string::npos);
}

TEST_SUITE("reboots")
{
    auto rebooted_at(uint64_t at)
    {
        return [at](uint64_t f) {
            auto s = healthy(f);
            s.reboots = 7 + (f >= at ? 1 : 0); // the count before the run started is just a baseline
            return s;
        };
    }

    TEST_CASE("a reboot is caught on the frame it shows up, as a heuristic")
    {
        CrashDetector d;
        auto v = feed(d, 1, 600, rebooted_at(240));
        REQUIRE(v);
        CHECK(v->signal == CrashSignal::Reboot);
        CHECK(v->frame == 240);
        CHECK(v->since_frame == 240);
        CHECK(v->heuristic);
        CHECK(v->text.find("reboot") != std::string::npos);
        CHECK(std::string(crash_signal_name(CrashSignal::Reboot)) == "reboot");
    }

    TEST_CASE("each reboot is reported once")
    {
        CrashDetector d;
        int verdicts = 0;
        auto make = rebooted_at(100);
        for (uint64_t f = 1; f < 400; f++)
            verdicts += d.observe(f, make(f)).has_value();
        CHECK(verdicts == 1);
    }

    TEST_CASE("after a reset the next count is only a baseline")
    {
        CrashDetector d;
        auto make = rebooted_at(100);
        CHECK_FALSE(feed(d, 1, 50, make));
        d.reset();
        CHECK_FALSE(feed(d, 150, 100, make)); // already 8 when it looks again: nothing new
    }

    TEST_CASE("switched off, a reboot is ignored")
    {
        DetectorConfig cfg;
        cfg.reboot = false;
        CrashDetector d(cfg);
        CHECK_FALSE(feed(d, 1, 600, rebooted_at(240)));
    }
}
