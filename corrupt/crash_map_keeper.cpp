#include "crash_map_keeper.h"

namespace corrupt
{

void CrashMapKeeper::reset(CrashMaps loaded)
{
    own = std::move(loaded.mine);
    others = std::move(loaded.others);
    learner.reset();
    dirty = crash_unsaved = false;
    frames_since_save = 0;
    rebuild();
}

void CrashMapKeeper::observe(const std::deque<LoggedEvent> &log, uint64_t frame)
{
    // Counting survivals into a scratch map tells whether anything changed.
    CrashMap learned;
    learner.observe(log, frame, learned);
    if (!learned.empty())
    {
        own.merge(learned);
        all.merge(learned);
        dirty = true;
    }
    frames_since_save++;
}

bool CrashMapKeeper::crashed(const GuardOutcome &out)
{
    if (out.kind == GuardOutcome::Nothing)
        return false;
    CrashMap learned;
    learner.crash(out.blame_after, out.blame_through, out.restored_frame, learned);
    if (learned.empty())
        return false;
    own.merge(learned);
    dirty = crash_unsaved = true;
    return rebuild();
}

bool CrashMapKeeper::save_due() const
{
    return dirty && (crash_unsaved || frames_since_save >= kSaveEveryFrames);
}

bool CrashMapKeeper::saved()
{
    dirty = crash_unsaved = false;
    frames_since_save = 0;
    return rebuild(); // survivals may have cooled a block since the last crash
}

std::vector<float> CrashMapKeeper::danger(Target t, size_t size) const
{
    std::vector<float> out(size, 0.0f);
    for (auto &[block, counts] : all.blocks(t))
        for (size_t i = block; i < std::min<size_t>(size, size_t(block) + CrashMap::kBlock); i++)
            out[i] = float(CrashMap::danger_of(counts));
    return out;
}

std::vector<BlockCounts> CrashMapKeeper::counts_per_block(Target t, size_t size) const
{
    std::vector<BlockCounts> out((size + CrashMap::kBlock - 1) / CrashMap::kBlock);
    for (auto &[block, counts] : all.blocks(t))
        if (block / CrashMap::kBlock < out.size())
            out[block / CrashMap::kBlock] = counts;
    return out;
}

std::vector<uint8_t> CrashMapKeeper::hot_per_block(Target t, size_t size) const
{
    std::vector<uint8_t> out((size + CrashMap::kBlock - 1) / CrashMap::kBlock, 0);
    for (size_t i = 0; i < out.size(); i++)
        out[i] = mask.is_hot(t, uint32_t(i * CrashMap::kBlock)) ? 1 : 0;
    return out;
}

bool CrashMapKeeper::rebuild()
{
    all = others;
    all.merge(own);
    HotMask fresh = corrupt::hot_mask(all);
    bool changed = fresh.hot != mask.hot;
    mask = std::move(fresh);
    return changed;
}

} // namespace corrupt
