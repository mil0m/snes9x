#include "core_access.h"
#include "opcodes.h"

#include <array>
#include <utility>

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

// The code map's tracer: every opcode in the CPU's six tables wrapped, so each instruction
// run from ROM marks its bytes. The fast tables fix M and X (E1, M1X1, M1X0, M0X1, M0X0),
// so each wrapper knows its instruction's length and width at compile time, and finds the
// opcode at CPU.PCBase + PC - 1 (those tables only run where PCBase is valid and the
// instruction doesn't cross a 4 KB block). The slow table reads the flags at run time and
// looks each byte up through the memory map.
namespace rom_trace
{

constexpr int kTables = 6;
constexpr int kSlow = 5;
SOpcodes *const tables[kTables] = { S9xOpcodesE1, S9xOpcodesM1X1, S9xOpcodesM1X0,
                                    S9xOpcodesM0X1, S9xOpcodesM0X0, S9xOpcodesSlow };
void (*original[kTables][256])(void) = {};
bool installed = false;
uint8 *flags = nullptr; // the map being recorded into, sized like the ROM

constexpr uint8_t width_flag(uint8_t op, bool m8, bool x8)
{
    switch (op_width(op))
    {
    case WidthBy::M: return m8 ? codeflag::Op8 : codeflag::Op16;
    case WidthBy::X: return x8 ? codeflag::Op8 : codeflag::Op16;
    case WidthBy::None: return 0;
    }
    return 0;
}

const uint8 *byte_at(uint32 address)
{
    uint8 *block = Memory.Map[(address & 0xFFFFFF) >> MEMMAP_SHIFT];
    if (reinterpret_cast<uintptr_t>(block) < CMemory::MAP_LAST)
        return nullptr;
    return block + (address & 0xFFFF);
}

void trace_slow(uint8 op)
{
    bool e = CheckEmulation();
    bool m8 = e || CheckMemory();
    bool x8 = e || CheckIndex();
    int length = op_length(op, m8, x8);
    uint8 w = width_flag(op, m8, x8);
    uint16 pc = uint16(Registers.PCw - 1);
    S9xRomFetchFrom = S9xRomFetchTo = 0;
    for (int i = 0; i < length; i++)
    {
        const uint8 *p = byte_at((uint32(Registers.PB) << 16) | uint16(pc + i));
        uintptr_t o = uintptr_t(p) - uintptr_t(Memory.ROM);
        if (!p || o >= Memory.CalculatedSize)
            continue;
        flags[o] |= (i ? codeflag::Operand : codeflag::Opcode) | w | codeflag::Touched;
        if (i == 0)
        {
            S9xRomFetchFrom = uint32(o);
            S9xRomFetchTo = uint32(o + length);
        }
    }
}

template <int T, int Op>
void traced()
{
    if constexpr (T == kSlow)
        trace_slow(uint8(Op));
    else
    {
        constexpr bool m8 = T == 0 || T == 1 || T == 2;
        constexpr bool x8 = T == 0 || T == 1 || T == 3;
        constexpr int length = op_length(uint8_t(Op), m8, x8);
        constexpr uint8 w = width_flag(uint8_t(Op), m8, x8);
        uintptr_t o = uintptr_t(CPU.PCBase + uint16(Registers.PCw - 1)) - uintptr_t(Memory.ROM);
        if (o < Memory.CalculatedSize && length <= Memory.CalculatedSize - o)
        {
            uint8 *f = flags + o;
            f[0] |= codeflag::Opcode | w | codeflag::Touched;
            for (int i = 1; i < length; i++)
                f[i] |= codeflag::Operand | w | codeflag::Touched;
            S9xRomFetchFrom = uint32(o);
            S9xRomFetchTo = uint32(o + length);
        }
        else
            S9xRomFetchFrom = S9xRomFetchTo = 0;
    }
    original[T][Op]();
}

template <int T, size_t... Ops>
constexpr std::array<void (*)(void), 256> wrapper_row(std::index_sequence<Ops...>)
{
    return { &traced<T, int(Ops)>... };
}

const std::array<void (*)(void), 256> wrappers[kTables] = {
    wrapper_row<0>(std::make_index_sequence<256>()), wrapper_row<1>(std::make_index_sequence<256>()),
    wrapper_row<2>(std::make_index_sequence<256>()), wrapper_row<3>(std::make_index_sequence<256>()),
    wrapper_row<4>(std::make_index_sequence<256>()), wrapper_row<5>(std::make_index_sequence<256>()),
};

// Puts the wrappers in, saving whatever the tables hold (the reset watch's wrapper too).
void wrap()
{
    for (int t = 0; t < kTables; t++)
        for (int op = 0; op < 256; op++)
        {
            original[t][op] = tables[t][op].S9xOpcode;
            tables[t][op].S9xOpcode = wrappers[t][op];
        }
    installed = true;
}

void unwrap()
{
    if (!installed)
        return;
    for (int t = 0; t < kTables; t++)
        for (int op = 0; op < 256; op++)
            tables[t][op].S9xOpcode = original[t][op];
    installed = false;
}

// Anything else that edits the tables (the reset watch) does it with the tracer lifted,
// so neither ever saves or restores the other's stale entry.
template <typename F>
void lifted(F &&edit)
{
    bool was = installed;
    unwrap();
    edit();
    if (was)
        wrap();
}

} // namespace rom_trace

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

void unhook()
{
    if (!installed)
        return;
    for (int t = 0; t < kTables; t++)
        tables[t][opcode].S9xOpcode = original[t];
    installed = false;
}

void hook()
{
    unhook();
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

void uninstall()
{
    rom_trace::lifted(unhook);
}

void install()
{
    rom_trace::lifted(hook);
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

std::vector<uint8_t> SnesCoreAccess::rom_image(bool fix_checksum)
{
    if (!Memory.ROM || !Memory.CalculatedSize)
        return {};
    std::vector<uint8_t> image(Memory.ROM, Memory.ROM + Memory.CalculatedSize);
    auto header = locked_rom();
    if (!fix_checksum || header.empty() || header[0].first + 0x30 > image.size())
        return image;
    // $FFDC-$FFDF: complement, then checksum, little-endian. A matching pair always adds
    // 0x1FE to the sum, so set one first, sum the way the core does on load, then fill it in.
    uint8_t *fields = image.data() + header[0].first + 0x2C;
    fields[0] = fields[1] = 0xFF;
    fields[2] = fields[3] = 0x00;
    uint16 sum;
    if (image.size() & 0x7FFF)
        sum = Memory.checksum_calc_sum(image.data(), uint32(image.size()));
    else
    {
        uint32 length = uint32(image.size());
        sum = Memory.checksum_mirror_sum(image.data(), length);
    }
    fields[0] = uint8_t(~sum & 0xFF);
    fields[1] = uint8_t(~sum >> 8);
    fields[2] = uint8_t(sum & 0xFF);
    fields[3] = uint8_t(sum >> 8);
    return image;
}

RomAddresses SnesCoreAccess::rom_addresses()
{
    RomAddresses out;
    if (!Memory.ROM || !Memory.CalculatedSize)
        return out;
    size_t chunks = (Memory.CalculatedSize + RomAddresses::kChunk - 1) / RomAddresses::kChunk;
    out.chunks.assign(chunks, RomAddresses::kNone);
    std::vector<int> best(chunks, 0);
    for (uint32 block = 0; block < 0x1000; block++)
    {
        uint32 address = block << MEMMAP_SHIFT;
        const uint8 *p = reset_watch::byte_at(address);
        uintptr_t o = uintptr_t(p) - uintptr_t(Memory.ROM);
        if (!p || o >= Memory.CalculatedSize || o % RomAddresses::kChunk)
            continue;
        uint32 bank = address >> 16;
        bool a15 = address & 0x8000;
        int score = Memory.HiROM ? (bank >= 0xC0 ? 4 : bank >= 0x40 && bank < 0x7E ? 3 : bank >= 0x80 && a15 ? 2 : 1)
                                 : (bank >= 0x80 && a15 ? 4 : a15 && bank < 0x7E ? 3 : bank >= 0xC0 ? 2 : 1);
        size_t chunk = o / RomAddresses::kChunk;
        if (score > best[chunk])
        {
            best[chunk] = score;
            out.chunks[chunk] = address;
        }
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

bool RomTracer::start(CodeMap &map)
{
    if (rom_trace::installed || !Memory.ROM || map.size() != Memory.CalculatedSize || map.empty())
        return false;
    rom_trace::flags = map.data();
    S9xRomFetchFrom = S9xRomFetchTo = 0;
    S9xRomFlags = map.data();
    rom_trace::wrap();
    mine = true;
    return true;
}

void RomTracer::stop()
{
    if (!mine)
        return;
    rom_trace::unwrap();
    S9xRomFlags = nullptr;
    rom_trace::flags = nullptr;
    mine = false;
}

std::vector<uintptr_t> RomTracer::opcode_tables()
{
    std::vector<uintptr_t> out;
    for (auto *table : rom_trace::tables)
        for (int op = 0; op < 256; op++)
            out.push_back(reinterpret_cast<uintptr_t>(table[op].S9xOpcode));
    return out;
}

} // namespace corrupt
