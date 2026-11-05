#include "picture.h"

#include <bitset>
#include <cstdlib>

namespace corrupt
{

namespace
{

int distance(const uint8_t *a, const uint8_t *b)
{
    return std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]);
}

} // namespace

double differing_share(const Picture &a, const Picture &b, int threshold)
{
    if (a.w != b.w || a.h != b.h || a.empty() || b.empty())
        return 1.0;
    size_t n = size_t(a.w) * a.h, differ = 0;
    for (size_t i = 0; i < n; i++)
        differ += distance(&a.rgb[i * 3], &b.rgb[i * 3]) > threshold;
    return n ? double(differ) / double(n) : 0.0;
}

Detail detail_of(const Picture &p, int threshold)
{
    Detail d;
    if (p.empty())
        return d;
    std::bitset<4096> seen;
    size_t edges = 0, pairs = 0;
    for (int y = 0; y < p.h; y++)
        for (int x = 0; x < p.w; x++)
        {
            const uint8_t *c = p.at(x, y);
            seen.set((c[0] >> 4) << 8 | (c[1] >> 4) << 4 | (c[2] >> 4));
            if (x + 1 < p.w)
            {
                edges += distance(c, p.at(x + 1, y)) > threshold;
                pairs++;
            }
            if (y + 1 < p.h)
            {
                edges += distance(c, p.at(x, y + 1)) > threshold;
                pairs++;
            }
        }
    d.colours = uint32_t(seen.count());
    d.edges = pairs ? double(edges) / double(pairs) : 0.0;
    return d;
}

uint64_t picture_hash(const Picture &p)
{
    uint64_t h = 0xcbf29ce484222325ULL ^ (uint64_t(p.w) << 16 | p.h);
    for (uint8_t v : p.rgb)
        h = (h ^ v) * 0x100000001b3ULL;
    return h;
}

} // namespace corrupt
