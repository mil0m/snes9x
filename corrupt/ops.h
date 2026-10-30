#pragma once
#include "memory.h"
#include "rng.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace corrupt
{

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
};

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
};

// Per-rule state that has to survive between firings.
struct OpState
{
    std::unordered_map<uint64_t, uint8_t> held; // freeze: (target, offset) -> value
};

std::vector<WriteEvent> apply_op(const Op &op, OpState &state, MemoryAccess &mem,
                                 const AddressRange &range, const std::vector<uint32_t> &addresses,
                                 Rng &rng);

std::optional<std::string> validate_op(const Op &op, const AddressRange &range, const MemoryAccess &mem);

} // namespace corrupt
