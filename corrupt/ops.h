#pragma once
#include "memory.h"
#include "rng.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace corrupt
{

struct HotMask;

// One byte the engine changed. Only real changes are logged (old != new);
// phase 2 attributes crashes from these.
struct WriteEvent
{
    Target target;
    uint32_t offset;
    uint8_t old_value;
    uint8_t new_value;
};

enum class OpType
{
    Add,     // add a random delta in [min, max], wrap or clamp
    Random,  // write a random value in [min, max]
    Bitflip, // XOR with mask, or flip `bits` random bits
    Swap,    // swap a<->b (fixed) or each picked address with a random one
    Copy,    // copy `length` bytes from a random spot in `from` to each picked address
    Freeze,  // hold `value`, or the value seen when first hit
    Shuffle, // permute the `window` bytes starting at each picked address

    // Area ops: act on the rule's whole range or rectangle at once (area_ops.h).
    Shift,  // move it by (dx, dy), wrapping or filling with `value`
    Flip,   // mirror it along `axis`
    Levels, // remap every value through `curve` (with `amount`)
    Filter, // a 3x3 `kernel` over the 2D layout
    Stamp,  // copy the same-sized region at `source` onto it
    Fill,   // set every byte to `value`

    // Code ops: ROM only, on opcodes the code map knows (code_aim.h).
    OpcodeSwap, // another opcode of the same length and addressing mode
    BranchFlip, // a conditional branch becomes its opposite
    NopOut,     // the whole instruction becomes NOPs

    // Jump op: ROM only, on jump table entries the tracer found (pointer_swap.h).
    PointerSwap, // an entry gets the target of another entry its dispatcher went to

    // Function op: ROM only, on functions the call graph found (calls.h, #34).
    Kill, // a return (RTS or RTL, as it's called) at a function's entry: the function is skipped
};

constexpr bool is_jump_op(OpType t) { return t == OpType::PointerSwap; }
constexpr bool is_function_op(OpType t) { return t == OpType::Kill; }

enum class Axis
{
    Horizontal, // each row reversed
    Vertical,   // the rows in reverse order
    Both,
};

enum class Curve
{
    Invert,    // 255 - v
    Posterize, // `amount` levels
    Threshold, // 255 from `amount` up, else 0
    Gamma,     // `amount` above 1 brightens
    Contrast,  // `amount` -1..1
};

enum class Kernel
{
    Blur,
    Sharpen,
    Median,
    Emboss,
    Edges,
    Max,
    Min,
};

const char *axis_name(Axis a);
std::optional<Axis> parse_axis(std::string_view name);
const char *curve_name(Curve c);
std::optional<Curve> parse_curve(std::string_view name);
const char *kernel_name(Kernel k);
std::optional<Kernel> parse_kernel(std::string_view name);

const char *op_name(OpType t);
std::optional<OpType> parse_op(std::string_view name);

struct Op
{
    OpType type = OpType::Random;
    int min = 0;
    int max = 0;
    bool clamp = false;
    int bits = 1;
    std::optional<uint8_t> mask;
    int width = 1;
    std::optional<uint32_t> a;
    std::optional<uint32_t> b;
    std::optional<AddressRange> from;
    uint32_t length = 1;
    std::optional<uint8_t> value;
    int window = 8;
    // Area ops.
    int dx = 0;
    int dy = 0;
    bool wrap = true;
    Axis axis = Axis::Horizontal;
    Curve curve = Curve::Invert;
    double amount = 0.0;
    Kernel kernel = Kernel::Blur;
    uint32_t source = 0; // stamp: the top-left of the region copied from
    // Pointer swap.
    bool likely = false; // also take targets from entries nobody jumped through yet
    bool mutual = false; // both ways: the donor's entry gets this entry's target
    // Kill: pick from any function learned, not only the 64 that ran in the most frames.
    bool any = false;
    // Kill: put each function back on after this many frames (#61); 0: it stays off.
    uint32_t put_back = 0;
};

// Per-rule state that has to survive between firings.
struct OpState
{
    std::unordered_map<uint64_t, uint8_t> held; // freeze: (target, offset) -> value
};

// With skip, bytes it marks hot are left alone. The values are still drawn for them, so the
// other addresses get exactly the writes they would have without it.
std::vector<WriteEvent> apply_op(const Op &op, OpState &state, MemoryAccess &mem,
                                 const AddressRange &range, const std::vector<uint32_t> &addresses,
                                 Rng &rng, const HotMask *skip = nullptr);

std::optional<std::string> validate_op(const Op &op, const AddressRange &range, const MemoryAccess &mem);

} // namespace corrupt
