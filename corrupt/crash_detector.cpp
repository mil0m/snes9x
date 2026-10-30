#include "crash_detector.h"

#include <cstdio>

namespace corrupt
{

namespace
{

std::string seconds(uint64_t frames)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f s", frames / 60.0);
    return buf;
}

// Tracks how long a condition has held: returns its start frame while true.
std::optional<uint64_t> track(std::optional<uint64_t> &since, bool condition, uint64_t frame)
{
    if (!condition)
    {
        since.reset();
        return std::nullopt;
    }
    if (!since)
        since = frame;
    return since;
}

} // namespace

const char *crash_signal_name(CrashSignal s)
{
    switch (s)
    {
    case CrashSignal::CpuStopped: return "cpu_stopped";
    case CrashSignal::Deadlock: return "deadlock";
    case CrashSignal::RunawayPc: return "runaway_pc";
    case CrashSignal::NmiOff: return "nmi_off";
    case CrashSignal::FrozenPicture: return "frozen_picture";
    case CrashSignal::BlackScreen: return "black_screen";
    case CrashSignal::Manual: return "manual";
    }
    return "?";
}

std::string describe_pc(uint32_t pc)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "$%02X:%04X", (pc >> 16) & 0xFF, pc & 0xFFFF);
    return buf;
}

void CrashDetector::reset()
{
    runaway_since.reset();
    nmi_off_since.reset();
    black_since.reset();
    frozen_since.reset();
    last_hash.reset();
}

std::optional<CrashVerdict> CrashDetector::observe(uint64_t frame, const FrameSignals &s)
{
    if (!config.enabled)
        return std::nullopt;

    // Definite crashes.
    if (config.cpu_stopped && s.cpu_halted)
    {
        reset();
        if (s.emulation_stopped)
            return CrashVerdict{ CrashSignal::Deadlock, frame, frame, false, "CPU deadlocked" };
        return CrashVerdict{ CrashSignal::CpuStopped, frame, frame, false, "CPU stopped (STP at " + describe_pc(s.pc) + ")" };
    }

    if (auto since = track(runaway_since, config.runaway_frames && s.pc_class != PcClass::Ok, frame);
        since && frame - *since + 1 >= config.runaway_frames)
    {
        std::string where = s.pc_class == PcClass::Io ? "I/O registers" : "unmapped memory";
        runaway_since.reset();
        return CrashVerdict{ CrashSignal::RunawayPc, frame, *since, false, "running code from " + where + " at " + describe_pc(s.pc) };
    }

    // Heuristics.
    if (auto since = track(nmi_off_since, config.nmi_off_frames && !s.nmi_enabled, frame);
        since && frame - *since + 1 >= config.nmi_off_frames)
    {
        nmi_off_since.reset();
        return CrashVerdict{ CrashSignal::NmiOff, frame, *since, true, "frame interrupt off for " + seconds(frame - *since + 1) };
    }

    bool black = s.forced_blank || s.brightness == 0;
    if (auto since = track(black_since, config.black_frames && black, frame);
        since && frame - *since + 1 >= config.black_frames)
    {
        black_since.reset();
        return CrashVerdict{ CrashSignal::BlackScreen, frame, *since, true, "black screen for " + seconds(frame - *since + 1) };
    }

    // A frozen picture counts repeats of the same picture; black screens have their own check.
    bool repeat = !black && last_hash && *last_hash == s.picture_hash;
    if (!repeat)
        frozen_since = black ? std::nullopt : std::optional<uint64_t>(frame);
    last_hash = s.picture_hash;
    if (config.frozen_frames && repeat && frozen_since && frame - *frozen_since >= config.frozen_frames)
    {
        uint64_t since = *frozen_since;
        frozen_since = frame; // report again only after another full stretch
        return CrashVerdict{ CrashSignal::FrozenPicture, frame, since, true, "picture frozen for " + seconds(frame - since) };
    }

    return std::nullopt;
}

} // namespace corrupt
