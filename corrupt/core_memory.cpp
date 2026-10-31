#include "core_memory.h"

#include "snes9x.h"
#include "memmap.h"
#include "ppu.h"
#include "gfx.h"
#include "apu/apu.h"

namespace corrupt
{

namespace
{

void invalidate_tiles(uint32_t address)
{
    // Mirrors what the core does after a VRAM write ($2118/$2119).
    IPPU.TileCached[TILE_2BIT][address >> 4] = FALSE;
    IPPU.TileCached[TILE_4BIT][address >> 5] = FALSE;
    IPPU.TileCached[TILE_8BIT][address >> 6] = FALSE;
    IPPU.TileCached[TILE_2BIT_EVEN][address >> 4] = FALSE;
    IPPU.TileCached[TILE_2BIT_EVEN][((address >> 4) - 1) & (MAX_2BIT_TILES - 1)] = FALSE;
    IPPU.TileCached[TILE_2BIT_ODD][address >> 4] = FALSE;
    IPPU.TileCached[TILE_2BIT_ODD][((address >> 4) - 1) & (MAX_2BIT_TILES - 1)] = FALSE;
    IPPU.TileCached[TILE_4BIT_EVEN][address >> 5] = FALSE;
    IPPU.TileCached[TILE_4BIT_EVEN][((address >> 5) - 1) & (MAX_4BIT_TILES - 1)] = FALSE;
    IPPU.TileCached[TILE_4BIT_ODD][address >> 5] = FALSE;
    IPPU.TileCached[TILE_4BIT_ODD][((address >> 5) - 1) & (MAX_4BIT_TILES - 1)] = FALSE;
}

void write_palette_byte(uint32_t offset, uint8_t value)
{
    // CGRAM is 256 little-endian 15-bit colours; mirrors the core's $2122 path.
    uint32_t index = offset >> 1;
    uint16_t colour = PPU.CGDATA[index];
    if (offset & 1)
        colour = uint16_t((colour & 0x00FF) | ((value & 0x7F) << 8));
    else
        colour = uint16_t((colour & 0x7F00) | value);
    if (colour == PPU.CGDATA[index])
        return;

    FLUSH_REDRAW();
    PPU.CGDATA[index] = colour;
    IPPU.ColorsChanged = TRUE;
    IPPU.Red[index] = IPPU.XB[colour & 0x1F];
    IPPU.Green[index] = IPPU.XB[(colour >> 5) & 0x1F];
    IPPU.Blue[index] = IPPU.XB[(colour >> 10) & 0x1F];
    IPPU.ScreenColors[index] = uint16(BUILD_PIXEL(IPPU.Red[index], IPPU.Green[index], IPPU.Blue[index]));
}

} // namespace

size_t CoreMemory::size(Target t) const
{
    switch (t)
    {
    case Target::WRAM: return sizeof(Memory.RAM);
    case Target::SRAM:
        if (!Memory.SRAM || !Memory.SRAMSize)
            return 0;
        return std::min<size_t>(size_t(1 << (Memory.SRAMSize + 3)) * 128, Memory.SRAM_SIZE);
    case Target::VRAM: return sizeof(Memory.VRAM);
    case Target::OAM: return sizeof(PPU.OAMData);
    case Target::CGRAM: return sizeof(PPU.CGDATA);
    case Target::ARAM: return S9xAPUGetRAM() ? 0x10000 : 0;
    case Target::ROM: return Memory.ROM ? Memory.CalculatedSize : 0;
    }
    return 0;
}

const uint8_t *CoreMemory::data(Target t) const
{
    switch (t)
    {
    case Target::WRAM: return Memory.RAM;
    case Target::ROM: return Memory.ROM;
    default: return nullptr;
    }
}

uint8_t CoreMemory::read(Target t, uint32_t offset) const
{
    if (offset >= size(t))
        return 0;
    switch (t)
    {
    case Target::WRAM: return Memory.RAM[offset];
    case Target::SRAM: return Memory.SRAM[offset];
    case Target::VRAM: return Memory.VRAM[offset];
    case Target::OAM: return PPU.OAMData[offset];
    case Target::CGRAM:
    {
        uint16_t colour = PPU.CGDATA[offset >> 1];
        return (offset & 1) ? uint8_t(colour >> 8) : uint8_t(colour & 0xFF);
    }
    case Target::ARAM: return S9xAPUGetRAM()[offset];
    case Target::ROM: return Memory.ROM[offset];
    }
    return 0;
}

void CoreMemory::write(Target t, uint32_t offset, uint8_t value)
{
    if (offset >= size(t))
        return;
    switch (t)
    {
    case Target::WRAM:
        Memory.RAM[offset] = value;
        break;
    case Target::SRAM:
        Memory.SRAM[offset] = value;
        break;
    case Target::VRAM:
        Memory.VRAM[offset] = value;
        invalidate_tiles(offset);
        break;
    case Target::OAM:
        PPU.OAMData[offset] = value;
        IPPU.OBJChanged = TRUE;
        break;
    case Target::CGRAM:
        write_palette_byte(offset, value);
        break;
    case Target::ARAM:
        S9xAPUGetRAM()[offset] = value;
        break;
    case Target::ROM:
        // The CPU reads the ROM through direct pointers and keeps no decoded copy, so the
        // next fetch sees the new byte. (Super FX and SA-1 caches aren't handled yet.)
        Memory.ROM[offset] = value;
        break;
    }
}

} // namespace corrupt
