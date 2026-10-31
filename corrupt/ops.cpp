#include "ops.h"
#include "crash_map.h"

#include <algorithm>

namespace corrupt
{

namespace
{

struct Writer
{
    MemoryAccess &mem;
    std::vector<WriteEvent> events;
    const HotMask *skip = nullptr;

    void put(Target t, uint32_t offset, uint8_t value)
    {
        if (offset >= mem.size(t) || (skip && skip->is_hot(t, offset)))
            return;
        uint8_t old = mem.read(t, offset);
        if (old == value)
            return;
        mem.write(t, offset, value);
        // Log what memory actually holds: hardware can drop bits (CGRAM has no bit 15).
        uint8_t stored = mem.read(t, offset);
        if (stored != old)
            events.push_back({ t, offset, old, stored });
    }
};

uint64_t key(Target t, uint32_t offset)
{
    return (uint64_t(t) << 32) | offset;
}

void swap_bytes(Writer &w, Target t, uint32_t p, uint32_t q, int width)
{
    if (p == q)
        return;
    for (int i = 0; i < width; i++)
    {
        uint8_t vp = w.mem.read(t, p + i);
        uint8_t vq = w.mem.read(t, q + i);
        w.put(t, p + i, vq);
        w.put(t, q + i, vp);
    }
}

} // namespace

const char *op_name(OpType t)
{
    switch (t)
    {
    case OpType::Add: return "add";
    case OpType::Random: return "random";
    case OpType::Bitflip: return "bitflip";
    case OpType::Swap: return "swap";
    case OpType::Copy: return "copy";
    case OpType::Freeze: return "freeze";
    case OpType::Shuffle: return "shuffle";
    case OpType::Shift: return "shift";
    case OpType::Flip: return "flip";
    case OpType::Levels: return "levels";
    case OpType::Filter: return "filter";
    case OpType::Stamp: return "stamp";
    case OpType::Fill: return "fill";
    case OpType::OpcodeSwap: return "opcode_swap";
    case OpType::BranchFlip: return "branch_flip";
    case OpType::NopOut: return "nop_out";
    }
    return "?";
}

const char *axis_name(Axis a)
{
    switch (a)
    {
    case Axis::Horizontal: return "horizontal";
    case Axis::Vertical: return "vertical";
    case Axis::Both: return "both";
    }
    return "?";
}

std::optional<Axis> parse_axis(std::string_view name)
{
    for (auto a : { Axis::Horizontal, Axis::Vertical, Axis::Both })
        if (name == axis_name(a))
            return a;
    return std::nullopt;
}

const char *curve_name(Curve c)
{
    switch (c)
    {
    case Curve::Invert: return "invert";
    case Curve::Posterize: return "posterize";
    case Curve::Threshold: return "threshold";
    case Curve::Gamma: return "gamma";
    case Curve::Contrast: return "contrast";
    }
    return "?";
}

std::optional<Curve> parse_curve(std::string_view name)
{
    for (auto c : { Curve::Invert, Curve::Posterize, Curve::Threshold, Curve::Gamma, Curve::Contrast })
        if (name == curve_name(c))
            return c;
    return std::nullopt;
}

const char *kernel_name(Kernel k)
{
    switch (k)
    {
    case Kernel::Blur: return "blur";
    case Kernel::Sharpen: return "sharpen";
    case Kernel::Median: return "median";
    case Kernel::Emboss: return "emboss";
    case Kernel::Edges: return "edges";
    case Kernel::Max: return "max";
    case Kernel::Min: return "min";
    }
    return "?";
}

std::optional<Kernel> parse_kernel(std::string_view name)
{
    for (auto k : { Kernel::Blur, Kernel::Sharpen, Kernel::Median, Kernel::Emboss, Kernel::Edges, Kernel::Max, Kernel::Min })
        if (name == kernel_name(k))
            return k;
    return std::nullopt;
}

std::optional<OpType> parse_op(std::string_view name)
{
    for (auto t : { OpType::Add, OpType::Random, OpType::Bitflip, OpType::Swap, OpType::Copy, OpType::Freeze,
                    OpType::Shuffle, OpType::Shift, OpType::Flip, OpType::Levels, OpType::Filter, OpType::Stamp,
                    OpType::Fill, OpType::OpcodeSwap, OpType::BranchFlip, OpType::NopOut })
        if (name == op_name(t))
            return t;
    return std::nullopt;
}

std::vector<WriteEvent> apply_op(const Op &op, OpState &state, MemoryAccess &mem,
                                 const AddressRange &range, const std::vector<uint32_t> &addresses,
                                 Rng &rng, const HotMask *skip)
{
    Writer w{ mem, {}, skip };
    const Target t = range.target;

    switch (op.type)
    {
    case OpType::Add:
        for (auto p : addresses)
        {
            int v = mem.read(t, p) + int(rng.range(op.min, op.max));
            v = op.clamp ? std::clamp(v, 0, 255) : (v & 0xFF);
            w.put(t, p, uint8_t(v));
        }
        break;

    case OpType::Random:
        for (auto p : addresses)
            w.put(t, p, uint8_t(rng.range(op.min, op.max)));
        break;

    case OpType::Bitflip:
        for (auto p : addresses)
        {
            uint8_t mask = 0;
            if (op.mask)
                mask = *op.mask;
            else
            {
                int order[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
                for (int i = 7; i > 0; i--)
                    std::swap(order[i], order[rng.range(0, i)]);
                for (int i = 0; i < op.bits; i++)
                    mask |= uint8_t(1 << order[i]);
            }
            w.put(t, p, mem.read(t, p) ^ mask);
        }
        break;

    case OpType::Swap:
        if (op.a && op.b)
            swap_bytes(w, t, *op.a, *op.b, op.width);
        else
        {
            uint32_t last_start = range.last - uint32_t(op.width - 1);
            for (auto p : addresses)
            {
                uint32_t q = uint32_t(rng.range(range.first, last_start));
                swap_bytes(w, t, std::min(p, last_start), q, op.width);
            }
        }
        break;

    case OpType::Copy:
    {
        if (!op.from)
            break;
        const auto &from = *op.from;
        uint32_t last_src = from.last - (op.length - 1);
        for (auto p : addresses)
        {
            uint32_t src = uint32_t(rng.range(from.first, last_src));
            std::vector<uint8_t> block;
            for (uint32_t i = 0; i < op.length; i++)
                block.push_back(mem.read(from.target, src + i));
            for (uint32_t i = 0; i < op.length && p + i <= range.last; i++)
                w.put(t, p + i, block[i]);
        }
        break;
    }

    case OpType::Freeze:
        for (auto p : addresses)
        {
            uint8_t hold;
            if (op.value)
                hold = *op.value;
            else
            {
                auto [it, inserted] = state.held.try_emplace(key(t, p), mem.read(t, p));
                hold = it->second;
            }
            w.put(t, p, hold);
        }
        break;

    case OpType::Shuffle:
        for (auto p : addresses)
        {
            uint32_t end = std::min<uint32_t>(p + uint32_t(op.window) - 1, range.last);
            std::vector<uint8_t> bytes;
            for (uint32_t a = p; a <= end; a++)
                bytes.push_back(mem.read(t, a));
            for (size_t i = bytes.size() - 1; i > 0; i--)
                std::swap(bytes[i], bytes[size_t(rng.range(0, int64_t(i)))]);
            for (uint32_t a = p; a <= end; a++)
                w.put(t, a, bytes[a - p]);
        }
        break;

    case OpType::Shift:
    case OpType::Flip:
    case OpType::Levels:
    case OpType::Filter:
    case OpType::Stamp:
    case OpType::Fill:
        break; // area ops: apply_area_op
    case OpType::OpcodeSwap:
    case OpType::BranchFlip:
    case OpType::NopOut:
        break; // code ops: apply_code_op
    }

    return w.events;
}

std::optional<std::string> validate_op(const Op &op, const AddressRange &range, const MemoryAccess &mem)
{
    auto size = mem.size(range.target);

    switch (op.type)
    {
    case OpType::Add:
        if (op.min > op.max)
            return "min must not be above max";
        if (op.min < -255 || op.max > 255)
            return "min and max must be within -255..255";
        break;

    case OpType::Random:
        if (op.min < 0 || op.min > 255)
            return "min must be within 0..255, got " + std::to_string(op.min);
        if (op.max < 0 || op.max > 255)
            return "max must be within 0..255, got " + std::to_string(op.max);
        if (op.min > op.max)
            return "min must not be above max";
        break;

    case OpType::Bitflip:
        if (!op.mask && (op.bits < 1 || op.bits > 8))
            return "bits must be within 1..8";
        break;

    case OpType::Swap:
        if (op.width != 1 && op.width != 2)
            return "width must be 1 or 2";
        if (op.a.has_value() != op.b.has_value())
            return "a fixed swap needs both a and b";
        if (op.a && (*op.a + op.width - 1 >= size || *op.b + op.width - 1 >= size))
            return "a and b must fit inside " + std::string(target_name(range.target));
        if (!op.a && range.length() < uint32_t(op.width))
            return "range is smaller than width";
        break;

    case OpType::Copy:
        if (!op.from)
            return "copy needs a 'from' range";
        if (auto err = validate_range(*op.from, mem))
            return "from: " + *err;
        if (op.length < 1 || op.length > op.from->length())
            return "length must be within 1.." + std::to_string(op.from->length());
        break;

    case OpType::Freeze:
        break;

    case OpType::Shuffle:
        if (op.window < 2 || op.window > 256)
            return "window must be within 2..256";
        break;

    case OpType::Shift:
    case OpType::Flip:
    case OpType::Levels:
    case OpType::Filter:
    case OpType::Stamp:
    case OpType::Fill:
        break; // area ops: validate_area_op, which knows the rectangle

    case OpType::OpcodeSwap:
    case OpType::BranchFlip:
    case OpType::NopOut:
        if (range.target != Target::ROM)
            return std::string(op_name(op.type)) + " works on the rom target only";
        break;
    }
    return std::nullopt;
}

} // namespace corrupt
