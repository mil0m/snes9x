#include "envelope.h"

#include "rng.h"

#include <algorithm>
#include <bit>
#include <cstdlib>

namespace corrupt
{

namespace
{

constexpr uint16_t kMenus = 0x3000; // Start and Select

std::string button_name(uint16_t b)
{
    switch (b)
    {
    case 0x8000: return "B";
    case 0x4000: return "Y";
    case 0x0800: return "Up";
    case 0x0400: return "Down";
    case 0x0200: return "Left";
    case 0x0100: return "Right";
    case 0x0080: return "A";
    case 0x0040: return "X";
    case 0x0020: return "L";
    case 0x0010: return "R";
    }
    return "?";
}

// Sets pad 1's buttons but keeps its Start and Select as they were.
void set_buttons(Pads &p, const Pads &was, uint16_t buttons)
{
    p[0] = uint16_t((buttons & ~kMenus) | (was[0] & kMenus));
}

struct Sprite
{
    int x, y, tile;
};

std::vector<Sprite> visible_sprites(const std::vector<uint8_t> &o)
{
    std::vector<Sprite> out;
    if (o.size() < kOamSize)
        return out;
    for (int i = 0; i < 128; i++)
    {
        int high = (o[512 + size_t(i) / 4] >> ((i % 4) * 2)) & 1;
        int x = o[size_t(i) * 4] | high << 8;
        int y = o[size_t(i) * 4 + 1];
        int tile = o[size_t(i) * 4 + 2] | (o[size_t(i) * 4 + 3] & 1) << 8;
        if (x < 256 && y < 224)
            out.push_back({ x, y, tile });
    }
    return out;
}

uint32_t colour_of(const uint8_t *c)
{
    return uint32_t(c[0] >> 4) << 8 | uint32_t(c[1] >> 4) << 4 | uint32_t(c[2] >> 4);
}

} // namespace

std::vector<Pads> perturb(const std::vector<Pads> &pads, uint64_t seed, std::string *what)
{
    static const uint16_t kButtons[] = { 0x8000, 0x4000, 0x0800, 0x0400, 0x0200,
                                         0x0100, 0x0080, 0x0040, 0x0020, 0x0010 };
    Rng rng(seed * 0x9E3779B97F4A7C15ULL + 0x51AC3B0);
    size_t n = pads.size();
    size_t lo = std::max<size_t>(n / 20, 1), hi = std::max(n / 4, lo + 1);
    auto said = [&](std::string s) {
        if (what)
            *what = std::move(s);
    };
    for (int attempt = 0; attempt < 40 && n > 1; attempt++)
    {
        auto p = pads;
        int kind = attempt < 30 ? int(rng.range(0, 2)) : 2;
        if (kind == 0) // shift a press change
        {
            std::vector<size_t> edges;
            for (size_t f = lo; f < hi && f < n; f++)
                if ((pads[f][0] & ~kMenus) != (pads[f - 1][0] & ~kMenus))
                    edges.push_back(f);
            if (edges.empty())
                continue;
            size_t f = edges[size_t(rng.range(0, int64_t(edges.size()) - 1))];
            int d = int(rng.range(1, 4)) * (rng.chance(0.5) ? 1 : -1);
            if (d > 0)
                for (size_t i = f; i < std::min(n, f + size_t(d)); i++)
                    set_buttons(p[i], pads[i], pads[f - 1][0]);
            else
                for (size_t i = f - std::min(f, size_t(-d)); i < f; i++)
                    set_buttons(p[i], pads[i], pads[f][0]);
            if (p == pads)
                continue;
            said("shifted the press change at frame " + std::to_string(f) + " by " + std::to_string(d) + " frames");
            return p;
        }
        if (kind == 1) // drop a held press
        {
            std::vector<size_t> held;
            for (size_t f = lo; f < hi && f < n; f++)
                if (pads[f][0] & ~kMenus)
                    held.push_back(f);
            if (held.empty())
                continue;
            size_t f = held[size_t(rng.range(0, int64_t(held.size()) - 1))];
            size_t a = f, b = f + 1;
            while (a > lo && pads[a - 1][0] == pads[f][0] && f - a < 25)
                a--;
            while (b < n && pads[b][0] == pads[f][0] && b - a < 50)
                b++;
            for (size_t i = a; i < b; i++)
                set_buttons(p[i], pads[i], 0);
            said("dropped the press at frames " + std::to_string(a) + "-" + std::to_string(b - 1));
            return p;
        }
        // add a short press
        size_t f = size_t(rng.range(int64_t(lo), int64_t(hi) - 1));
        size_t len = size_t(rng.range(5, 20));
        uint16_t button = kButtons[rng.range(0, 9)];
        for (size_t i = f; i < std::min(n, f + len); i++)
            p[i][0] |= button;
        if (p == pads)
            continue;
        said("added " + button_name(button) + " at frames " + std::to_string(f) + "-" +
             std::to_string(std::min(n, f + len) - 1));
        return p;
    }
    said("");
    return pads;
}

double sprite_jump_rate(const std::vector<std::vector<uint8_t>> &oam, int far)
{
    size_t moves = 0, jumps = 0;
    std::vector<Sprite> before;
    for (size_t t = 0; t < oam.size(); t++)
    {
        auto now = visible_sprites(oam[t]);
        if (t > 0)
            for (auto &s : now)
            {
                int nearest = -1;
                for (auto &b : before)
                    if (b.tile == s.tile)
                    {
                        int d = std::abs(b.x - s.x) + std::abs(b.y - s.y);
                        if (nearest < 0 || d < nearest)
                            nearest = d;
                    }
                if (nearest < 0)
                    continue;
                moves++;
                jumps += nearest > far;
            }
        before = std::move(now);
    }
    return moves ? double(jumps) / double(moves) : 0.0;
}

void Envelope::add(const RunRecord &run, const StateTrace &trace)
{
    size_t n = run.thumbs.size();
    frames = pictures.empty() ? n : std::min(frames, n);
    std::vector<Picture> kept(n);
    for (size_t f = n / 2; f < n; f += kThumbEvery)
        kept[f] = run.thumbs[f];
    pictures.push_back(std::move(kept));
    for (auto &p : run.thumbs)
        for (size_t i = 0; i + 2 < p.rgb.size(); i += 3)
            colours.set(colour_of(&p.rgb[i]));

    for (auto &sample : trace.wram)
    {
        if (bytes.empty())
            bytes.resize(sample.size());
        for (size_t i = 0; i < sample.size() && i < bytes.size(); i++)
        {
            uint8_t v = sample[i];
            auto &b = bytes[i];
            b.seen[v >> 6] |= 1ULL << (v & 63);
            b.lo = std::min(b.lo, v);
            b.hi = std::max(b.hi, v);
        }
    }
    // Calls: the fewest frames over the runs so far (a run that didn't call it: 0).
    size_t before = pictures.size() - 1;
    for (auto &[entry, c] : calls)
        if (!trace.calls.count(entry))
            c.min = 0;
    for (auto &[entry, n_frames] : trace.calls)
    {
        auto [it, fresh] = calls.try_emplace(entry);
        auto &c = it->second;
        c.min = fresh ? (before == 0 ? n_frames : 0) : std::min(c.min, n_frames);
        c.max = std::max(c.max, n_frames);
        c.in_runs++;
    }
}

void Envelope::finish(std::span<const uint32_t> wram_reads)
{
    for (size_t i = 0; i < bytes.size(); i++)
    {
        auto &b = bytes[i];
        b.read = wram_reads.empty() || (i < wram_reads.size() && wram_reads[i] > 0);
        int values = 0;
        for (auto w : b.seen)
            values += std::popcount(w);
        b.steady = values > 0 && values <= kSteadyValues;
    }
}

Signals Envelope::measure(const RunRecord &run, const StateTrace &trace, std::span<const AddressRange> own) const
{
    Signals s;
    size_t n = run.thumbs.size();
    // Apart: against the nearest natural run.
    if (!pictures.empty() && n > 0)
    {
        double best = 1.0;
        for (auto &natural : pictures)
        {
            double sum = 0;
            size_t count = 0;
            for (size_t f = n / 2; f < n && f < natural.size(); f++)
                if (!natural[f].empty())
                {
                    sum += differing_share(run.thumbs[f], natural[f]);
                    count++;
                }
            if (count)
                best = std::min(best, sum / double(count));
        }
        s.apart = best;
    }
    // New colours: over the second half.
    if (n > 0 && colours.any())
    {
        double sum = 0;
        for (size_t f = n / 2; f < n; f++)
        {
            auto &p = run.thumbs[f];
            size_t fresh = 0, px = p.rgb.size() / 3;
            for (size_t i = 0; i < px; i++)
                fresh += !colours.test(colour_of(&p.rgb[i * 3]));
            sum += px ? double(fresh) / double(px) : 0.0;
        }
        s.new_colours = sum / double(n - n / 2);
    }
    // Bytes.
    if (!bytes.empty())
    {
        std::vector<uint8_t> mark(bytes.size(), 0); // 1: skip, 2: broken, 4: out of range
        for (auto &r : own)
            if (r.target == Target::WRAM)
                for (size_t i = r.first; i <= r.last && i < mark.size(); i++)
                    mark[i] = 1;
        for (size_t i = 0; i < bytes.size(); i++)
            if (!bytes[i].read || bytes[i].lo > bytes[i].hi)
                mark[i] = 1;
        for (auto &sample : trace.wram)
            for (size_t i = 0; i < sample.size() && i < bytes.size(); i++)
            {
                if (mark[i] & 1)
                    continue;
                uint8_t v = sample[i];
                auto &b = bytes[i];
                if (b.steady)
                {
                    if (!(b.seen[v >> 6] & (1ULL << (v & 63))))
                        mark[i] |= 2;
                }
                else
                {
                    int m = std::max(1, (b.hi - b.lo) / 4);
                    if (v < b.lo - m || v > b.hi + m)
                        mark[i] |= 4;
                }
            }
        for (auto m : mark)
        {
            s.steady_broken += (m & 2) != 0;
            s.out_of_range += (m & 4) != 0;
        }
    }
    // Functions.
    size_t runs_in = pictures.size();
    for (auto &[entry, f] : trace.calls)
        if (f > 0 && !calls.count(entry))
            s.new_functions++;
    uint32_t often = std::max<uint32_t>(1, uint32_t(frames / 4));
    for (auto &[entry, c] : calls)
        if (c.in_runs == runs_in && c.min >= often)
        {
            auto it = trace.calls.find(entry);
            uint32_t f = it == trace.calls.end() ? 0 : it->second;
            s.silenced += f * 2 < c.min;
        }
    s.sprite_jumps = sprite_jump_rate(trace.oam);
    return s;
}

} // namespace corrupt
