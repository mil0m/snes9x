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
    last_input.reset();
    black_presses = 0;
    frozen_presses = 0;
}

std::optional<CrashVerdict> CrashDetector::observe(uint64_t frame, const FrameSignals &s)
{
    if (!config.enabled)
        return std::nullopt;

    bool pressed = last_input && *last_input != s.input;
    last_input = s.input;

    // Definite crashes.
    if (config.cpu_stopped && s.cpu_halted)
    {
        reset();
        if (s.emulation_stopped && s.opcode != uint8_t(0xDB)) // STP also trips the core's watchdog
            return CrashVerdict{ CrashSignal::Deadlock, frame, frame, false, "CPU deadlocked at " + describe_pc(s.pc) };
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
    bool was_black = black_since.has_value();
    if (auto since = track(black_since, config.black_frames && black, frame))
    {
        black_presses = was_black ? black_presses + pressed : 0;
        if (frame - *since + 1 >= config.black_frames && black_presses >= config.min_presses)
        {
            black_since.reset();
            return CrashVerdict{ CrashSignal::BlackScreen, frame, *since, true,
                                 "black screen for " + seconds(frame - *since + 1) + " despite button presses" };
        }
    }

    // A frozen picture counts repeats of the same picture; black screens have their own
    // check, and frames that weren't drawn carry no picture information at all.
    if (frozen_since && pressed)
        frozen_presses++;
    if (!s.picture_valid)
        return std::nullopt;
    bool repeat = !black && last_hash && *last_hash == s.picture_hash;
    if (!repeat)
    {
        frozen_since = black ? std::nullopt : std::optional<uint64_t>(frame);
        frozen_presses = 0;
    }
    last_hash = s.picture_hash;
    if (config.frozen_frames && repeat && frozen_since && frame - *frozen_since >= config.frozen_frames &&
        frozen_presses >= config.min_presses)
    {
        uint64_t since = *frozen_since;
        frozen_since = frame; // report again only after another full stretch
        frozen_presses = 0;
        return CrashVerdict{ CrashSignal::FrozenPicture, frame, since, true,
                             "picture frozen for " + seconds(frame - since) + " despite button presses" };
    }

    return std::nullopt;
}

} // namespace corrupt
