#include "crash_learner.h"

#include <algorithm>
#include <unordered_set>

namespace corrupt
{

void CrashLearner::forget_after(uint64_t frame)
{
    while (!writes.empty() && writes.back().frame > frame)
        writes.pop_back();
    seen = frame;
}

void CrashLearner::observe(const std::deque<LoggedEvent> &log, uint64_t frame, CrashMap &map)
{
    if (frame < seen)
        forget_after(frame);

    // The log is in frame order; only the tail is new.
    auto it = log.end();
    while (it != log.begin() && std::prev(it)->frame > seen)
        --it;
    for (; it != log.end(); ++it)
        if (it->frame <= frame && !(no_rom && it->write.target == Target::ROM))
            writes.push_back({ it->frame, it->write.target, it->write.offset });
    seen = std::max(seen, frame);

    while (!writes.empty() && writes.front().frame + kSurviveFrames <= frame)
    {
        auto &w = writes.front();
        auto [last, fresh] = last_survival.try_emplace(key(w.target, w.offset), w.frame);
        if (fresh || w.frame >= last->second + kSurvivalSpacing)
        {
            map.add_survival(w.target, w.offset);
            last->second = w.frame;
        }
        writes.pop_front();
    }
}

void CrashLearner::crash(uint64_t after, uint64_t through, uint64_t restored, CrashMap &map)
{
    after = std::max(after, through > kBlameFrames ? through - kBlameFrames : 0);
    std::unordered_set<uint64_t> blamed;
    for (auto &w : writes)
        if (w.frame > after && w.frame <= through && blamed.insert(key(w.target, w.offset)).second)
            map.add_crash(w.target, w.offset);
    // Blamed writes are all after the checkpoint the guard rolled back to, so they go too.
    forget_after(restored);
}

void CrashLearner::reset()
{
    writes.clear();
    seen = 0;
    last_survival.clear();
}

} // namespace corrupt
