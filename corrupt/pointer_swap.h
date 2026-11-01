#pragma once
#include "jump_tables.h"
#include "ops.h"

#include <vector>

// The pointer swap op (roadmap #32): a jump table entry gets the target of another entry
// the same dispatcher went to, so the jump still lands on the first instruction of a real
// routine. Plan: docs/corruption/2026-11-01-pointer-swap-plan.md.
namespace corrupt
{

struct HotMask;

// Another target an entry may get, and the entry that holds it (for a two-way swap).
struct SwapDonor
{
    uint32_t target = 0;
    uint32_t offset = 0;
    int stride = 2;
};

// One entry a pointer swap rule may change, with what it may change to.
struct SwapSlot
{
    uint32_t offset = 0; // the entry's first byte
    int stride = 2;
    uint32_t target = 0; // what it holds in the clean ROM
    std::vector<SwapDonor> donors;
};

// The entries the game jumped through that lie wholly in [first, last], each with the other
// targets its dispatcher went to (from any of its tables; with `likely`, entries nobody
// jumped through yet as well), that fit the entry (a 2-byte entry keeps its bank). Entries
// with nothing to swap with are left out. In ROM order; donors in ROM order, one per target.
std::vector<SwapSlot> swap_slots(const std::vector<JumpTable> &tables, uint32_t first, uint32_t last, bool likely);

// Writes a donor's target into each picked slot (an index into `slots`), the donor drawn at
// random; with `mutual`, the slot's target into the donor's entry too. An entry with any
// byte `skip` marks is left whole, never half-written.
std::vector<WriteEvent> apply_pointer_swap(const std::vector<SwapSlot> &slots, const std::vector<uint32_t> &picks,
                                           bool mutual, MemoryAccess &mem, Rng &rng, const HotMask *skip = nullptr);

} // namespace corrupt
