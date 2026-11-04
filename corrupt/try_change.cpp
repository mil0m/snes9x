#include "try_change.h"

#include <cstdio>

namespace corrupt
{

namespace
{

std::string secs(uint32_t frames)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%.1f s", frames / 60.0);
    return buf;
}

uint32_t moves(std::span<const uint64_t> pictures, size_t from, size_t to)
{
    uint32_t n = 0;
    for (size_t i = from + 1; i < to && i < pictures.size(); i++)
        n += pictures[i] != pictures[i - 1];
    return n;
}

} // namespace

bool control_moves(std::span<const uint64_t> control, const FreezeConfig &cfg)
{
    if (control.size() < cfg.hold || cfg.hold == 0)
        return false;
    return moves(control, control.size() - cfg.hold, control.size()) >= cfg.min_moves;
}

std::optional<uint32_t> froze_at(std::span<const uint64_t> run, std::span<const uint64_t> control,
                                 const FreezeConfig &cfg)
{
    if (cfg.hold == 0 || run.size() < cfg.hold || control.size() < run.size())
        return std::nullopt;
    size_t end = run.size();
    uint64_t last = run[end - 1];
    for (size_t i = end - cfg.hold; i < end; i++)
        if (run[i] != last)
            return std::nullopt;
    if (moves(control, end - cfg.hold, end) < cfg.min_moves)
        return std::nullopt; // still without the change too: nothing to tell
    size_t began = end - cfg.hold;
    while (began > 0 && run[began - 1] == last)
        began--;
    return uint32_t(began);
}

TryResult judge_try(const RunVerdict &with, const RunVerdict &without, uint32_t frames, bool use_known,
                    const FreezeConfig &cfg)
{
    TryResult r;
    r.frames = frames;
    std::string window = secs(frames);
    if (with.kind == RunVerdict::Inconclusive && with.frames == 0)
    {
        r.text = "It couldn't be tried: the game's state couldn't be saved.";
        return r;
    }
    if (without.kind != RunVerdict::Survived)
    {
        r.kind = TryResult::Unclear;
        r.text = "Without this change the game also goes wrong within " + window + " (" + without.text +
                 "), so the test can't tell what this one does.";
        return r;
    }
    if (with.kind == RunVerdict::Inconclusive)
    {
        r.kind = TryResult::Unclear;
        r.text = "Something else went wrong in the game within " + window + " (" + with.text +
                 "), so the test can't tell what this one does.";
        return r;
    }
    if (with.kind == RunVerdict::Crashed)
    {
        r.kind = TryResult::Crashed;
        r.at = with.frames;
        r.text = "With it, the game crashed: " + with.text + ". Without it, it didn't.";
        return r;
    }
    if (auto at = froze_at(with.pictures, without.pictures, cfg))
    {
        r.kind = TryResult::Froze;
        r.at = *at;
        r.text = "With it, the game froze after " + secs(*at) +
                 ": the picture stopped, and without the change it kept moving.";
        return r;
    }
    if (use_known && !with.used)
    {
        r.kind = TryResult::Unused;
        r.text = "The game didn't run this code in " + window +
                 ", so the test says nothing yet. Try again where the game uses it.";
        return r;
    }
    r.kind = TryResult::Running;
    r.still_anyway = !control_moves(without.pictures, cfg);
    r.differs = !with.pictures.empty() && !without.pictures.empty() && with.pictures.back() != without.pictures.back();
    r.text = "Still running after " + window + (r.differs ? "; the picture differs from without it." : "; it looks the same as without it so far.");
    if (r.still_anyway)
        r.text += " The picture doesn't move without it either, so a freeze can't be told here.";
    return r;
}

TryResult try_change(RunAhead &runner, Engine &engine, RomKeeper &rom, CoreAccess &core, std::span<uint8_t> flags,
                     const AheadHooks &hooks, const Change &c, uint32_t frames, bool use_known, const FreezeConfig &cfg)
{
    TryResult failed;
    failed.frames = frames;
    if (!engine.active() || c.value.empty() || c.value.size() != c.was.size())
    {
        failed.text = "It can be tried only while corruption is on.";
        return failed;
    }
    std::vector<uint32_t> bytes;
    for (size_t i = 0; i < c.value.size(); i++)
        bytes.push_back(uint32_t(c.at + i));
    RunSpec spec;
    spec.watched = bytes;
    spec.frames = frames;
    spec.settle_frames = frames; // the whole window: a freeze can come after the first use
    spec.pictures = true;
    auto queued = engine.pending_flips();
    auto run = [&](bool on) {
        engine.set_pending_flips(queued);
        engine.flip_bytes({ c.at, c.value, c.was, on ? Engine::Flip::How::On : Engine::Flip::How::Off });
        auto v = runner.play(engine, rom, core, flags, hooks, spec);
        engine.set_pending_flips(queued);
        return v;
    };
    auto without = run(false);
    auto with = run(true);
    if (with.frames == 0 || without.frames == 0)
    {
        failed.text = "It couldn't be tried: the game's state couldn't be saved.";
        return failed;
    }
    return judge_try(with, without, frames, use_known, cfg);
}

} // namespace corrupt
