#include "core_access.h"

#include "snes9x.h"
#include "memmap.h"
#include "ppu.h"
#include "gfx.h"
#include "snapshot.h"
#include "movie.h"
#include "cpuexec.h"
#include "apu/bapu/snes/snes.hpp"

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

// The reset watch. The core runs each instruction through a table of opcode handlers
// (one table per CPU mode, plus a slow-path one), and the tables are plain globals, so
// the reset handler's first opcode can be wrapped without touching the core's source.
// Only that opcode pays for the check, and only a native-mode entry counts: a power-on
// or a reset from the front end enters in emulation mode.
namespace reset_watch
{

constexpr int kTables = 6;
SOpcodes *const tables[kTables] = { S9xOpcodesE1, S9xOpcodesM1X1, S9xOpcodesM1X0,
                                    S9xOpcodesM0X1, S9xOpcodesM0X0, S9xOpcodesSlow };
void (*original[kTables])(void) = {};
bool installed = false;
uint8 opcode = 0;
uint16 target_low = 0;
const uint8 *target = nullptr; // the reset handler's first byte, wherever it's mapped
uint32_t count = 0;

const uint8 *byte_at(uint32 address)
{
    uint8 *block = Memory.Map[(address & 0xFFFFFF) >> MEMMAP_SHIFT];
    if (reinterpret_cast<uintptr_t>(block) < CMemory::MAP_LAST)
        return nullptr; // I/O, open bus or special hardware: not plain memory
    return block + (address & 0xFFFF);
}

template <int T>
void wrapped()
{
    // The core has already stepped PC past the opcode.
    uint16 at = uint16(Registers.PCw - 1);
    if (at == target_low && !CheckEmulation() && byte_at((uint32(Registers.PB) << 16) | at) == target)
        count++;
    original[T]();
}

void (*const wrappers[kTables])(void) = { wrapped<0>, wrapped<1>, wrapped<2>, wrapped<3>, wrapped<4>, wrapped<5> };

void uninstall()
{
    if (!installed)
        return;
    for (int t = 0; t < kTables; t++)
        tables[t][opcode].S9xOpcode = original[t];
    installed = false;
}

void install()
{
    uninstall();
    const uint8 *lo = byte_at(0x00FFFC), *hi = byte_at(0x00FFFD);
    if (!lo || !hi)
        return;
    uint16 reset = uint16(*lo | (*hi << 8));
    target = byte_at(reset);
    if (!target)
        return;
    target_low = reset;
    opcode = *target;
    for (int t = 0; t < kTables; t++)
    {
        original[t] = tables[t][opcode].S9xOpcode;
        tables[t][opcode].S9xOpcode = wrappers[t];
    }
    installed = true;
}

} // namespace reset_watch

} // namespace

void SnesCoreAccess::reset_console()
{
    S9xReset();
    if (reset_watch::installed)
        reset_watch::install(); // the reset code's first opcode may be a different one now
}

std::vector<AddressRange> SnesCoreAccess::locked_rom()
{
    std::vector<AddressRange> out;
    if (!Memory.ROM)
        return out;
    for (uint32 a = 0x00FFB0; a <= 0x00FFFF; a++)
    {
        const uint8 *p = reset_watch::byte_at(a);
        if (!p || p < Memory.ROM || p >= Memory.ROM + Memory.CalculatedSize)
            continue;
        auto offset = uint32_t(p - Memory.ROM);
        if (!out.empty() && out.back().last + 1 == offset)
            out.back().last = offset;
        else
            out.push_back({ Target::ROM, offset, offset });
    }
    return out;
}

VideoLayout SnesCoreAccess::video_layout()
{
    VideoLayout v;
    v.mode = PPU.BGMode & 7;
    uint8 screens = Memory.FillRAM[0x212C] | Memory.FillRAM[0x212D]; // main and sub screen layers
    for (int i = 0; i < 4; i++)
    {
        // The core keeps these as VRAM word addresses.
        v.bg[i].tiles = uint32_t(PPU.BG[i].NameBase << 1) & 0xFFFF;
        v.bg[i].map = uint32_t(PPU.BG[i].SCBase << 1) & 0xFFFF;
        v.bg[i].map_size = uint8_t(PPU.BG[i].SCSize & 3);
        v.bg[i].shown = (screens >> i) & 1;
    }
    // Sprite bases are already byte addresses; tiles 256-511 sit 0x2000 on, plus the gap.
    v.sprite_tiles = PPU.OBJNameBase & 0xFFFF;
    v.sprite_tiles2 = (PPU.OBJNameBase + 0x2000 + PPU.OBJNameSelect) & 0xFFFF;
    return v;
}

SoundLayout SnesCoreAccess::sound_layout()
{
    SoundLayout s;
    s.echo_page = uint8_t(SNES::dsp.spc_dsp.read(0x6D));      // ESA
    s.echo_delay = uint8_t(SNES::dsp.spc_dsp.read(0x7D) & 15); // EDL
    s.echo_writes = !(SNES::dsp.spc_dsp.read(0x6C) & 0x20);    // FLG bit 5 switches echo writes off
    s.sample_dir_page = uint8_t(SNES::dsp.spc_dsp.read(0x5D)); // DIR
    return s;
}

void SnesCoreAccess::watch_resets()
{
    reset_watch::install();
}

void SnesCoreAccess::unwatch_resets()
{
    reset_watch::uninstall();
}

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
    s.reboots = reset_watch::count;
    return s;
}

void SnesCoreAccess::note_frame_start()
{
    drawing = IPPU.RenderThisFrame;
}

void WramAccessCounter::start()
{
    if (counting())
        return;
    reads.assign(sizeof(Memory.RAM), 0);
    writes.assign(sizeof(Memory.RAM), 0);
    S9xWramReads = reads.data();
    S9xWramWrites = writes.data();
}

void WramAccessCounter::stop()
{
    if (!counting())
        return;
    S9xWramReads = S9xWramWrites = nullptr;
    reads = {};
    writes = {};
}

void WramAccessCounter::take(MemoryWatch &watch)
{
    if (counting())
        watch.observe_accesses(reads, writes);
}

} // namespace corrupt
