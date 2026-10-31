#pragma once
#include "memory.h"
#include "ops.h"
#include "rng.h"

#include <optional>
#include <string>
#include <vector>

namespace corrupt
{

struct HotMask;

// A rule's rectangle: the range's first byte is its top-left corner; `row` is the row
// width it was drawn in (the pixel view's), `width` and `height` its size in bytes and rows.
struct Rect
{
    uint32_t row = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

// The bytes a rule covers, as a grid: a rectangle, or a plain range as one row.
struct Area
{
    Target target = Target::WRAM;
    uint32_t start = 0;
    uint32_t row = 1;
    uint32_t width = 1;
    uint32_t height = 1;

    uint32_t cells() const { return width * height; }
    uint32_t at(uint32_t x, uint32_t y) const { return start + y * row + x; }
    uint32_t cell(uint32_t k) const { return at(k % width, k / width); } // left to right, top to bottom
    uint32_t last() const { return at(width - 1, height - 1); }
};

Area area_of(const AddressRange &range, const std::optional<Rect> &rect);

bool is_area_op(OpType t);

// Applies an area op to the whole area at once: everything is read first, so a shift or a
// stamp onto itself works. Bytes `skip` marks are left alone; only real changes are logged.
std::vector<WriteEvent> apply_area_op(const Op &op, const Area &area, MemoryAccess &mem, Rng &rng,
                                      const HotMask *skip);

std::optional<std::string> validate_area(const Area &area, const MemoryAccess &mem);
std::optional<std::string> validate_area_op(const Op &op, const Area &area, const MemoryAccess &mem);

} // namespace corrupt
