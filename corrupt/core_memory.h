#pragma once
#include "memory.h"

namespace corrupt
{

// MemoryAccess over the live emulator. Writes keep the PPU's caches in step, so
// tile, sprite and palette corruption shows up on the next frame instead of
// whenever the game happens to redraw.
class CoreMemory : public MemoryAccess
{
  public:
    size_t size(Target t) const override;
    uint8_t read(Target t, uint32_t offset) const override;
    void write(Target t, uint32_t offset, uint8_t value) override;
};

} // namespace corrupt
