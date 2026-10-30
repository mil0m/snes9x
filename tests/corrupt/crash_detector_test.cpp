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
    CHECK_FALSE(feed(d, 51, 179, off));
    auto v = d.observe(230, off(230));
    REQUIRE(v);
    CHECK(v->signal == CrashSignal::NmiOff);
    CHECK(v->since_frame == 51);
    CHECK(v->heuristic);
}

TEST_CASE("a frozen picture is a heuristic crash")
{
    CrashDetector d;
    auto frozen = [](uint64_t) { FrameSignals s; s.picture_hash = 42; return s; };
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
    auto black = [](uint64_t) { FrameSignals s; s.forced_blank = true; s.picture_hash = 0; return s; };
    auto v = feed(d, 1, 1000, black);
    REQUIRE(v);
    CHECK(v->signal == CrashSignal::BlackScreen);
    CHECK(v->frame == 480);

    CrashDetector dim;
    auto zero_brightness = [](uint64_t) { FrameSignals s; s.brightness = 0; s.picture_hash = 0; return s; };
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
    auto frozen = [](uint64_t) { FrameSignals s; s.picture_hash = 42; return s; };
    feed(d, 1, 299, frozen);
    d.reset();
    CHECK_FALSE(feed(d, 300, 299, frozen));
}

TEST_CASE("after reporting, a still-broken game reports again only after a fresh run")
{
    CrashDetector d;
    auto frozen = [](uint64_t) { FrameSignals s; s.picture_hash = 42; return s; };
    REQUIRE(feed(d, 1, 400, frozen));
    CHECK_FALSE(d.observe(302, frozen(302))); // no repeat on the very next frame
}
