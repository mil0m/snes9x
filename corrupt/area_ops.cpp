#include "area_ops.h"
#include "crash_map.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace corrupt
{

namespace
{

uint8_t clamp_byte(double v)
{
    return uint8_t(std::clamp(std::lround(v), 0L, 255L));
}

std::array<uint8_t, 256> curve_table(Curve c, double amount)
{
    std::array<uint8_t, 256> t{};
    for (int v = 0; v < 256; v++)
    {
        double x = v;
        switch (c)
        {
        case Curve::Invert: x = 255 - v; break;
        case Curve::Posterize:
        {
            double steps = std::max(1.0, std::round(amount) - 1);
            x = std::round(std::round(v / 255.0 * steps) * 255.0 / steps);
            break;
        }
        case Curve::Threshold: x = v >= amount ? 255 : 0; break;
        case Curve::Gamma: x = 255.0 * std::pow(v / 255.0, 1.0 / std::max(amount, 0.01)); break;
        case Curve::Contrast:
        {
            double c2 = std::clamp(amount, -0.99, 0.99);
            x = 128.0 + (v - 128.0) * (1.0 + c2) / (1.0 - c2);
            break;
        }
        }
        t[size_t(v)] = clamp_byte(x);
    }
    return t;
}

// The 3x3 neighbourhood of (x, y), with the edges repeated.
std::array<int, 9> around(const std::vector<uint8_t> &grid, const Area &a, uint32_t x, uint32_t y)
{
    std::array<int, 9> n{};
    int i = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
        {
            int cx = std::clamp(int(x) + dx, 0, int(a.width) - 1);
            int cy = std::clamp(int(y) + dy, 0, int(a.height) - 1);
            n[size_t(i++)] = grid[size_t(cy) * a.width + size_t(cx)];
        }
    return n;
}

uint8_t filtered(Kernel k, const std::array<int, 9> &n)
{
    // n[4] is the centre; n[0] top-left, n[8] bottom-right.
    switch (k)
    {
    case Kernel::Blur:
    {
        int sum = 0;
        for (int v : n)
            sum += v;
        return clamp_byte(sum / 9.0);
    }
    case Kernel::Sharpen: return clamp_byte(5 * n[4] - n[1] - n[3] - n[5] - n[7]);
    case Kernel::Median:
    {
        auto s = n;
        std::nth_element(s.begin(), s.begin() + 4, s.end());
        return uint8_t(s[4]);
    }
    case Kernel::Emboss: return clamp_byte(128 + n[8] - n[0]);
    case Kernel::Edges:
    {
        int sum = 0;
        for (int i = 0; i < 9; i++)
            if (i != 4)
                sum += n[size_t(i)];
        return clamp_byte(std::abs(8 * n[4] - sum));
    }
    case Kernel::Max: return uint8_t(*std::max_element(n.begin(), n.end()));
    case Kernel::Min: return uint8_t(*std::min_element(n.begin(), n.end()));
    }
    return uint8_t(n[4]);
}

int wrap_index(int v, int n)
{
    return ((v % n) + n) % n;
}

} // namespace

Area area_of(const AddressRange &range, const std::optional<Rect> &rect)
{
    if (rect)
        return { range.target, range.first, rect->row, rect->width, rect->height };
    return { range.target, range.first, range.length(), range.length(), 1 };
}

bool is_area_op(OpType t)
{
    switch (t)
    {
    case OpType::Shift:
    case OpType::Flip:
    case OpType::Levels:
    case OpType::Filter:
    case OpType::Stamp:
    case OpType::Fill: return true;
    default: return false;
    }
}

std::vector<WriteEvent> apply_area_op(const Op &op, const Area &a, MemoryAccess &mem, Rng &, const HotMask *skip)
{
    std::vector<uint8_t> before(a.cells());
    for (uint32_t k = 0; k < a.cells(); k++)
        before[k] = mem.read(a.target, a.cell(k));
    std::vector<uint8_t> after = before;
    int w = int(a.width), h = int(a.height);

    switch (op.type)
    {
    case OpType::Shift:
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                int sx = x - op.dx, sy = y - op.dy;
                if (op.wrap)
                    after[size_t(y * w + x)] = before[size_t(wrap_index(sy, h) * w + wrap_index(sx, w))];
                else if (sx < 0 || sx >= w || sy < 0 || sy >= h)
                    after[size_t(y * w + x)] = op.value.value_or(0);
                else
                    after[size_t(y * w + x)] = before[size_t(sy * w + sx)];
            }
        break;
    case OpType::Flip:
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                int sx = op.axis == Axis::Vertical ? x : w - 1 - x;
                int sy = op.axis == Axis::Horizontal ? y : h - 1 - y;
                after[size_t(y * w + x)] = before[size_t(sy * w + sx)];
            }
        break;
    case OpType::Levels:
    {
        auto table = curve_table(op.curve, op.amount);
        for (auto &v : after)
            v = table[v];
        break;
    }
    case OpType::Filter:
        for (uint32_t y = 0; y < a.height; y++)
            for (uint32_t x = 0; x < a.width; x++)
                after[size_t(y) * a.width + x] = filtered(op.kernel, around(before, a, x, y));
        break;
    case OpType::Stamp:
    {
        Area from{ a.target, op.source, a.row, a.width, a.height };
        for (uint32_t k = 0; k < a.cells(); k++)
            after[k] = mem.read(a.target, from.cell(k));
        break;
    }
    case OpType::Fill:
        std::fill(after.begin(), after.end(), op.value.value_or(0));
        break;
    default: return {};
    }

    std::vector<WriteEvent> events;
    for (uint32_t k = 0; k < a.cells(); k++)
    {
        if (after[k] == before[k])
            continue;
        uint32_t p = a.cell(k);
        if (skip && skip->is_hot(a.target, p))
            continue;
        mem.write(a.target, p, after[k]);
        uint8_t stored = mem.read(a.target, p); // hardware can drop bits (CGRAM)
        if (stored != before[k])
            events.push_back({ a.target, p, before[k], stored });
    }
    return events;
}

std::optional<std::string> validate_area(const Area &a, const MemoryAccess &mem)
{
    if (a.width < 1 || a.height < 1)
        return "a rectangle needs a width and a height of at least 1";
    if (a.width > a.row)
        return "a rectangle can't be wider than its rows";
    uint64_t last = uint64_t(a.start) + uint64_t(a.height - 1) * a.row + a.width - 1;
    if (last >= mem.size(a.target))
        return "the rectangle runs past the end of " + std::string(target_name(a.target)) + " (" +
               hex(uint32_t(std::min<uint64_t>(last, UINT32_MAX))) + ")";
    return std::nullopt;
}

std::optional<std::string> validate_area_op(const Op &op, const Area &a, const MemoryAccess &mem)
{
    switch (op.type)
    {
    case OpType::Levels:
        if (op.curve == Curve::Posterize && (op.amount < 2 || op.amount > 128))
            return "posterize needs 2..128 levels";
        if (op.curve == Curve::Threshold && (op.amount < 0 || op.amount > 255))
            return "threshold must be within 0..255";
        if (op.curve == Curve::Gamma && (op.amount < 0.1 || op.amount > 10))
            return "gamma must be within 0.1..10";
        if (op.curve == Curve::Contrast && (op.amount < -1 || op.amount > 1))
            return "contrast must be within -1..1";
        break;
    case OpType::Stamp:
        if (auto err = validate_area({ a.target, op.source, a.row, a.width, a.height }, mem))
            return "from: " + *err;
        break;
    default: break;
    }
    return std::nullopt;
}

} // namespace corrupt
