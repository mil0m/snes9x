#include "proven_blame.h"

#include <algorithm>
#include <chrono>

namespace corrupt
{

void ProvenBlame::set_detector(const DetectorConfig &c)
{
    detect = c;
    detect.frozen_frames = 0;
    detect.black_frames = 0;
}

std::optional<Proof> ProvenBlame::prove(Engine &engine, CoreAccess &core, const CrashVerdict &verdict,
                                        const std::vector<ProofStart> &starts)
{
    if (!blame.active() || !rom.loaded() || flags.empty())
        return std::nullopt;
    auto t0 = std::chrono::steady_clock::now();
    Proof proof;
    uint64_t now = engine.current_frame();
    uint64_t until = verdict.frame + cfg.tail;
    const auto &clean = rom.clean();
    auto length = [&](size_t si) { return until > starts[si].frame ? until - starts[si].frame : 0; };
    auto affordable = [&](size_t si) {
        return proof.runs < cfg.max_runs && proof.frames + length(si) <= cfg.max_frames;
    };
    // Plays from start si with `changed` as they are and the rest of `all` clean. True: it
    // went wrong again (any crash, or a landing).
    auto crashes = [&](size_t si, const std::vector<uint32_t> &all, const std::vector<uint32_t> &changed) {
        RunSpec spec;
        spec.from = starts[si].state;
        spec.engine_from = starts[si].engine;
        spec.watched = changed;
        for (uint32_t o : all)
            if (o < clean.size() && std::find(changed.begin(), changed.end(), o) == changed.end())
                spec.borrowed.push_back({ o, clean[o] });
        // The ROM stays as it is now; the engine's ROM writes would only add to it.
        spec.quiet_after = starts[si].engine ? std::min(now, starts[si].engine->frame) : now;
        spec.frames = uint32_t(length(si));
        spec.settle_frames = spec.frames; // no stopping early: the crash comes when it comes
        spec.detect = detect;
        proof.runs++;
        proof.frames += spec.frames;
        return runner.play(engine, rom, core, flags, hooks, spec).kind != RunVerdict::Survived;
    };
    auto without = [](std::vector<uint32_t> from, const std::vector<uint32_t> &gone) {
        std::erase_if(from, [&](uint32_t o) { return std::find(gone.begin(), gone.end(), o) != gone.end(); });
        return from;
    };

    tries++;
    // Narrows `cands` (which, clean, prevent the crash from start si) down by halves, keeping
    // a half that crashes on its own. Nothing if it runs out of budget with too many left
    // to fix them all (better a guess than undoing most of the corruption).
    auto narrow = [&](size_t si, const std::vector<uint32_t> &cands) -> std::optional<std::vector<uint32_t>> {
        std::vector<uint32_t> suspects = cands;
        bool alone = cands.size() == 1;
        while (suspects.size() > 1 && affordable(si))
        {
            std::vector<uint32_t> half(suspects.begin(), suspects.begin() + std::ptrdiff_t(suspects.size() / 2));
            alone = crashes(si, cands, half);
            suspects = alone ? half : without(suspects, half);
        }
        if (suspects.size() == 1 && !alone && affordable(si))
            alone = crashes(si, cands, suspects);
        if (alone)
            return suspects;
        if (cands.size() <= kFixAllAtMost)
            return cands; // proven together, not narrowed: all of them
        return std::nullopt;
    };
    auto done = [&](size_t si, const std::vector<uint32_t> &culprits, bool unseen) {
        proof.start = si;
        proof.fixed = blame.fix(culprits);
        proofs++;
        unseen_culprits += unseen;
        proof.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return proof;
    };

    auto all = blame.changed_offsets();
    std::sort(all.begin(), all.end());
    bool reproduced = false;
    for (size_t si = 0; si < starts.size(); si++)
    {
        if (!starts[si].state || !length(si) || all.empty())
            continue;
        if (!reproduced)
        {
            // Does it come back at all? If not, it needed something a replay hasn't.
            if (!affordable(si))
            {
                over_budget++;
                return std::nullopt;
            }
            if (!crashes(si, all, all))
            {
                not_reproduced++;
                return std::nullopt;
            }
            reproduced = true;
        }
        // The bytes the game used since this start, then every changed byte (the game may
        // read some in ways the tracer doesn't see).
        auto used = blame.used_between(starts[si].frame, verdict.frame);
        std::sort(used.begin(), used.end());
        for (const auto *cands : { &used, &all })
        {
            if (cands->empty() || (cands == &all && all.size() == used.size()))
                continue;
            if (!affordable(si))
            {
                over_budget++;
                return std::nullopt;
            }
            if (crashes(si, *cands, {}))
                continue; // not (only) these
            if (auto culprits = narrow(si, *cands))
                return done(si, *culprits, cands == &all);
            over_budget++;
            return std::nullopt;
        }
    }
    (reproduced ? unexplained : no_candidates)++;
    return std::nullopt;
}

} // namespace corrupt
