#include "core_access.h"

#include "snes9x.h"
#include "memmap.h"
#include "ppu.h"
#include "gfx.h"
#include "snapshot.h"
#include "movie.h"

namespace corrupt
{

namespace
{

PcClass classify(uint32_t pc)
{
    auto block = reinterpret_cast<uintptr_t>(Memory.Map[(pc & 0xFFFFFF) >> MEMMAP_SHIFT]);
    if (block == CMemory::MAP_NONE)
        return PcClass::OpenBus;
    if (block == CMemory::MAP_CPU || block == CMemory::MAP_PPU)
        return PcClass::Io;
    return PcClass::Ok;
}

uint64_t picture_hash()
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int y = 0; y < IPPU.RenderedScreenHeight; y++)
    {
        const uint16 *row = GFX.Screen + y * GFX.RealPPL;
        for (int x = 0; x < IPPU.RenderedScreenWidth; x++)
        {
            h ^= row[x];
            h *= 0x100000001b3ULL;
        }
    }
    return h;
}

} // namespace

std::vector<uint8_t> SnesCoreAccess::save_state()
{
    std::vector<uint8_t> state(S9xFreezeSize());
    if (!S9xFreezeGameMem(state.data(), uint32(state.size())))
        state.clear();
    return state;
}

bool SnesCoreAccess::load_state(const std::vector<uint8_t> &state)
{
    if (state.empty() || S9xUnfreezeGameMem(state.data(), uint32(state.size())) != SUCCESS)
        return false;
    // The CPU flags come back from the state; the core's own stop switch doesn't.
    Settings.StopEmulation = FALSE;
    return true;
}

FrameSignals SnesCoreAccess::sample()
{
    FrameSignals s;
    s.cpu_halted = (CPU.Flags & HALTED_FLAG) != 0;
    s.emulation_stopped = Settings.StopEmulation;
    s.pc = Registers.PBPC & 0xFFFFFF;
    s.pc_class = classify(s.pc);
    auto block = Memory.Map[s.pc >> MEMMAP_SHIFT];
    if (reinterpret_cast<uintptr_t>(block) >= CMemory::MAP_LAST) // a plain pointer, safe to read
        s.opcode = block[s.pc & 0xFFFF];
    s.nmi_enabled = (Memory.FillRAM[0x4200] & 0x80) != 0;
    s.forced_blank = PPU.ForcedBlanking;
    s.brightness = PPU.Brightness;
    s.input = MovieGetJoypad(0);
    s.picture_valid = drawing;
    s.picture_hash = drawing ? picture_hash() : 0;
    return s;
}

void SnesCoreAccess::note_frame_start()
{
    drawing = IPPU.RenderThisFrame;
}

} // namespace corrupt
