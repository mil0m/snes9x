#include "memory_watch.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace corrupt
{

void MemoryWatch::watch(Target target, size_t size)
{
    snap = {};
    snap.target = target;
    snap.bytes.assign(size, 0);
    if (target != Target::ROM) // the ROM: no heat (only rules change it), hits once there are some
    {
        snap.heat.assign(size, 0.0f);
        snap.hits.assign(size, 0);
    }
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

    if (snap.target == Target::ROM)
    {
        if (auto raw = mem.data(Target::ROM))
        {
            if (std::memcmp(snap.bytes.data(), raw, size) != 0)
                std::memcpy(snap.bytes.data(), raw, size);
        }
        else
            for (size_t i = 0; i < size; i++)
                snap.bytes[i] = mem.read(snap.target, uint32_t(i));
    }
    else
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
        {
            if (snap.hits.size() != size)
                snap.hits.assign(size, 0);
            snap.hits[it->write.offset]++;
        }
    seen_frame = frame;
}

void MemoryWatch::observe_accesses(std::vector<uint32_t> &reads, std::vector<uint32_t> &writes)
{
    size_t n = snap.bytes.size();
    if (active && snap.target == Target::WRAM && reads.size() >= n && writes.size() >= n)
    {
        if (snap.reads.size() != n)
        {
            snap.reads.assign(n, 0.0f);
            snap.writes.assign(n, 0.0f);
        }
        constexpr float kNew = 1.0f - kDecay;
        for (size_t i = 0; i < n; i++)
        {
            snap.reads[i] = snap.reads[i] * kDecay + float(reads[i]) * kNew;
            snap.writes[i] = snap.writes[i] * kDecay + float(writes[i]) * kNew;
        }
    }
    std::fill(reads.begin(), reads.end(), 0);
    std::fill(writes.begin(), writes.end(), 0);
}

namespace
{

constexpr size_t kGram = 8;

uint64_t gram_at(const std::vector<uint8_t> &v, size_t i)
{
    uint64_t g;
    std::memcpy(&g, v.data() + i, kGram);
    return g;
}

bool interesting(const std::vector<uint8_t> &v, size_t i)
{
    uint8_t a = v[i], b = a;
    for (size_t k = 1; k < kGram; k++)
    {
        uint8_t x = v[i + k];
        if (x == a || x == b)
            continue;
        if (a == b)
            b = x;
        else
            return true; // a third value
    }
    return false;
}

} // namespace

std::vector<CopyRun> find_copies(const std::vector<uint8_t> &haystack, const std::vector<uint8_t> &needle,
                                 uint32_t min_run)
{
    std::vector<CopyRun> runs;
    if (needle.size() < kGram || haystack.size() < kGram)
        return runs;

    std::unordered_map<uint64_t, std::vector<uint32_t>> seeds;
    for (size_t n = 0; n + kGram <= needle.size(); n++)
        if (interesting(needle, n))
            seeds[gram_at(needle, n)].push_back(uint32_t(n));

    // Per diagonal (haystack offset - needle offset): where the last run found ends,
    // so a long copy is reported once, not once per seed inside it.
    std::unordered_map<int64_t, size_t> covered;
    for (size_t h = 0; h + kGram <= haystack.size(); h++)
    {
        auto it = seeds.find(gram_at(haystack, h));
        if (it == seeds.end())
            continue;
        for (uint32_t n : it->second)
        {
            int64_t diagonal = int64_t(h) - int64_t(n);
            if (auto c = covered.find(diagonal); c != covered.end() && h < c->second)
                continue;
            size_t start_h = h, start_n = n;
            while (start_h > 0 && start_n > 0 && haystack[start_h - 1] == needle[start_n - 1])
                start_h--, start_n--;
            size_t end_h = h, end_n = n;
            while (end_h < haystack.size() && end_n < needle.size() && haystack[end_h] == needle[end_n])
                end_h++, end_n++;
            covered[diagonal] = end_h;
            if (end_h - start_h >= min_run)
                runs.push_back({ uint32_t(start_h), uint32_t(start_n), uint32_t(end_h - start_h) });
        }
    }

    std::sort(runs.begin(), runs.end(), [](const CopyRun &a, const CopyRun &b) {
        return a.length != b.length ? a.length > b.length : a.haystack_offset < b.haystack_offset;
    });

    // Repetitive tables (sprite entries that look alike) match themselves at many
    // shifts. Keep a run only if at least half of it lies outside the longer runs kept.
    std::vector<CopyRun> kept;
    for (auto &r : runs)
    {
        uint32_t inside = 0;
        for (auto &k : kept)
        {
            uint32_t lo = std::max(r.haystack_offset, k.haystack_offset);
            uint32_t hi = std::min(r.haystack_offset + r.length, k.haystack_offset + k.length);
            if (hi > lo)
                inside += hi - lo;
        }
        if (inside * 2 <= r.length)
            kept.push_back(r);
    }
    return kept;
}

const RomChange *MemorySnapshot::rom_change(uint32_t offset) const
{
    auto it = std::lower_bound(rom_changes.begin(), rom_changes.end(), offset,
                               [](const RomChange &c, uint32_t o) { return c.offset < o; });
    return it != rom_changes.end() && it->offset == offset ? &*it : nullptr;
}

} // namespace corrupt
