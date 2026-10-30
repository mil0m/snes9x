#include "memory_watch.h"

namespace corrupt
{

void MemoryWatch::watch(Target target, size_t size)
{
    snap = {};
    snap.target = target;
    snap.bytes.assign(size, 0);
    snap.heat.assign(size, 0.0f);
    snap.hits.assign(size, 0);
    active = true;
    have_baseline = false;
    seen_frame = 0;
}

void MemoryWatch::stop()
{
    snap = {};
    active = false;
}

void MemoryWatch::clear_hits()
{
    std::fill(snap.hits.begin(), snap.hits.end(), 0);
}

void MemoryWatch::observe(const MemoryAccess &mem, const std::deque<LoggedEvent> &log, uint64_t frame)
{
    if (!active)
        return;

    size_t size = mem.size(snap.target);
    if (size != snap.bytes.size())
    {
        Target t = snap.target;
        watch(t, size);
    }

    for (size_t i = 0; i < size; i++)
    {
        uint8_t now = mem.read(snap.target, uint32_t(i));
        float h = snap.heat[i] * kDecay;
        if (have_baseline && now != snap.bytes[i])
            h += 1.0f;
        snap.heat[i] = h;
        snap.bytes[i] = now;
    }
    have_baseline = true;

    // New hits: events after the last frame we looked at. After a rollback the
    // engine's frame goes back, and so does ours; what was counted stays counted.
    if (frame < seen_frame)
        seen_frame = frame;
    for (auto it = log.rbegin(); it != log.rend() && it->frame > seen_frame; ++it)
        if (it->frame <= frame && it->write.target == snap.target && it->write.offset < size)
            snap.hits[it->write.offset]++;
    seen_frame = frame;
}

} // namespace corrupt
