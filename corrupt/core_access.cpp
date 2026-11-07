#include "core_access.h"
#include "derail.h"
#include "jump_tables.h"
#include "opcodes.h"

#include <algorithm>
#include <array>
#include <utility>

#include "snes9x.h"
#include "memmap.h"
#include "ppu.h"
#include "gfx.h"
#include "pixform.h"
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
uint8 *flags = nullptr;     // the map being recorded into, sized like the ROM
JumpLog *jumps = nullptr;   // where indirect jumps are noted, if anywhere
CallLog *calls = nullptr;   // where calls are noted, if anywhere (#26)
DerailWatch *derails = nullptr; // fed every instruction, for the instant revert, if anyone listens
// After a landing (#57): mark nothing at all, as before, instead of the changed bytes run.
bool blind_after_landing = false;
uint64_t seen_after_landing = 0; // changed bytes marked used only because of that

// A landing was noted: the rest of the frame is garbage, so stop teaching the map (the
// core's data and DMA marks too) until the note is taken between frames. Only the crash
// blame's mark goes on: a changed byte run after the landing is still used (#57).
void derailed(Landing kind)
{
    derails->land(kind, (uint32_t(Registers.PB) << 16) | uint16(Registers.PCw - 1));
    if (derails->derailed())
        S9xRomFlags = nullptr;
}

// The changed bytes among an instruction's (or a pointer's) bytes, for the watch's window.
inline void note_changed(uint32_t offset, int length)
{
    for (int i = 0; i < length; i++)
        if (flags[offset + i] & codeflag::Changed)
            derails->used(offset + uint32_t(i));
}

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

uint32_t rom_offset_of(uint32 address)
{
    const uint8 *p = byte_at(address);
    uintptr_t o = uintptr_t(p) - uintptr_t(Memory.ROM);
    return p && o < Memory.CalculatedSize ? uint32_t(o) : kNoEntry;
}

// An indirect jump is about to run (PC is past its opcode): note where it is, where its
// pointer is and where it goes. Jumps from code in RAM, or through I/O, aren't noted.
void note_jump(uint8 op)
{
    uint32 pb = uint32(Registers.PB) << 16;
    uint16 pc = Registers.PCw;
    uint32_t site = rom_offset_of(pb | uint16(pc - 1));
    const uint8 *lo = byte_at(pb | pc), *hi = byte_at(pb | uint16(pc + 1));
    if (site == kNoEntry || !lo || !hi)
        return;
    uint16 a = uint16(*lo | (*hi << 8));
    // The pointer: JMP/JSR (a,X) read it from the program bank, JMP (a) and JML [a] from bank 0.
    bool indexed = op == jumpop::JmpIndexed || op == jumpop::JsrIndexed;
    uint32 base = indexed ? pb : 0;
    uint16 at = indexed ? uint16(a + Registers.X.W) : a;
    const uint8 *p0 = byte_at(base | at), *p1 = byte_at(base | uint16(at + 1));
    const uint8 *p2 = op == jumpop::JmlPointer ? byte_at(base | uint16(at + 2)) : nullptr;
    if (!p0 || !p1 || (op == jumpop::JmlPointer && !p2))
        return;
    uint32_t pointer = rom_offset_of(base | at);
    if (derails) // a changed pointer opens the window as a changed instruction does
        for (int i = 0; i < (op == jumpop::JmlPointer ? 3 : 2); i++)
            if (uint32_t o = rom_offset_of(base | uint16(at + i)); o != kNoEntry)
                note_changed(o, 1);
    uint32_t bank = op == jumpop::JmlPointer ? uint32_t(*p2) << 16 : pb;
    uint32_t target = bank | uint32_t(*p1 << 8) | *p0;
    if (calls && op == callop::JsrIndexed)
        if (uint32_t entry = rom_offset_of(target); entry != kNoEntry)
            calls->note(site, op, entry);
    if (jumps)
        jumps->note(site, op, pointer, target);
}

// A JSR $abs or JSL $long from ROM is about to run (PC is past its opcode): note the call
// site and the function's entry, both ROM offsets. Calls into RAM or I/O aren't noted.
void note_call(uint8 op)
{
    uint32 pb = uint32(Registers.PB) << 16;
    uint16 pc = Registers.PCw;
    uint32_t site = rom_offset_of(pb | uint16(pc - 1));
    const uint8 *lo = byte_at(pb | pc), *hi = byte_at(pb | uint16(pc + 1));
    if (site == kNoEntry || !lo || !hi)
        return;
    uint32_t address = pb | uint32_t(*hi << 8) | *lo;
    if (op == callop::Jsl)
    {
        const uint8 *bank = byte_at(pb | uint16(pc + 2));
        if (!bank)
            return;
        address = (uint32_t(*bank) << 16) | uint32_t(*hi << 8) | *lo;
    }
    if (uint32_t entry = rom_offset_of(address); entry != kNoEntry)
        calls->note(site, op, entry);
}

// After a landing: a changed byte the instruction holds gets Touched, nothing else.
inline void see_changed(uint8 &f)
{
    if ((f & (codeflag::Changed | codeflag::Touched)) == codeflag::Changed)
    {
        f |= codeflag::Touched;
        seen_after_landing++;
    }
}

void see_changed_slow(uint8 op)
{
    bool e = CheckEmulation();
    int length = op_length(op, e || CheckMemory(), e || CheckIndex());
    uint32 pb = uint32(Registers.PB) << 16;
    uint16 pc = uint16(Registers.PCw - 1);
    for (int i = 0; i < length; i++)
        if (uint32_t o = rom_offset_of(pb | uint16(pc + i)); o != kNoEntry)
            see_changed(flags[o]);
}

void trace_slow(uint8 op)
{
    if ((jumps || derails || calls) && jumpop::noted(op))
        note_jump(op);
    if (calls && (op == callop::Jsr || op == callop::Jsl))
        note_call(op);
    bool e = CheckEmulation();
    bool m8 = e || CheckMemory();
    bool x8 = e || CheckIndex();
    int length = op_length(op, m8, x8);
    uint8 w = width_flag(op, m8, x8);
    uint16 pc = uint16(Registers.PCw - 1);
    uint32 at = (uint32(Registers.PB) << 16) | pc;
    S9xRomFetchFrom = S9xRomFetchTo = 0;
    if (derails)
    {
        derails->step();
        const uint8 *p = byte_at(at);
        uintptr_t o = uintptr_t(p) - uintptr_t(Memory.ROM);
        bool in_rom = p && o < Memory.CalculatedSize;
        if (in_rom)
            for (int i = 0; i < length; i++)
                if (uint32_t oi = rom_offset_of((uint32(Registers.PB) << 16) | uint16(pc + i)); oi != kNoEntry)
                    note_changed(oi, 1);
        if (derails->open())
        {
            // Only open bus and I/O aren't plain memory to run from; SRAM and the like are.
            PcClass where = classify(at);
            Landing l = landing(where == PcClass::Ok, in_rom, in_rom ? flags[o] : 0, op);
            if (l != Landing::None)
            {
                derailed(l);
                return;
            }
        }
    }
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
    if (derails && derails->derailed())
    {
        // Garbage until the frame ends: the map learns nothing (the core's own marks are off
        // too), but the blame still sees the changed bytes run.
        S9xRomFetchFrom = S9xRomFetchTo = 0;
        if (!blind_after_landing)
        {
            if constexpr (T == kSlow)
                see_changed_slow(uint8(Op));
            else
            {
                constexpr bool m8 = T == 0 || T == 1 || T == 2;
                constexpr bool x8 = T == 0 || T == 1 || T == 3;
                constexpr int length = op_length(uint8_t(Op), m8, x8);
                uintptr_t o = uintptr_t(CPU.PCBase + uint16(Registers.PCw - 1)) - uintptr_t(Memory.ROM);
                if (o < Memory.CalculatedSize && length <= Memory.CalculatedSize - o)
                    for (int i = 0; i < length; i++)
                        see_changed(flags[o + i]);
            }
        }
        original[T][Op]();
        return;
    }
    if constexpr (T != kSlow && jumpop::noted(uint8_t(Op)))
        if (jumps || derails || calls)
            note_jump(uint8(Op));
    if constexpr (T != kSlow && (Op == callop::Jsr || Op == callop::Jsl))
        if (calls)
            note_call(uint8(Op));
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
            if (derails)
            {
                derails->step();
                uint8 any = 0;
                for (int i = 0; i < length; i++)
                    any |= f[i];
                if (any & codeflag::Changed)
                    note_changed(uint32_t(o), length);
                if (derails->open())
                    if (Landing l = landing(true, true, f[0], uint8_t(Op)); l != Landing::None)
                    {
                        derailed(l);
                        S9xRomFetchFrom = S9xRomFetchTo = 0;
                        original[T][Op]();
                        return;
                    }
            }
            f[0] |= codeflag::Opcode | w | codeflag::Touched;
            for (int i = 1; i < length; i++)
                f[i] |= codeflag::Operand | w | codeflag::Touched;
            S9xRomFetchFrom = uint32(o);
            S9xRomFetchTo = uint32(o + length);
        }
        else
        {
            // Not the ROM (RAM, SRAM): no map, but a few opcodes no game runs on purpose.
            if (derails)
            {
                derails->step();
                if (derails->open())
                    if (Landing l = landing(true, false, 0, uint8_t(Op)); l != Landing::None)
                        derailed(l);
            }
            S9xRomFetchFrom = S9xRomFetchTo = 0;
        }
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
    loads++;
    S9xReset();
    if (reset_watch::installed)
        reset_watch::install(); // the reset code's first opcode may be a different one now
}

Pads SnesCoreAccess::pads()
{
    Pads p{};
    for (int i = 0; i < int(p.size()); i++)
        p[i] = MovieGetJoypad(i);
    return p;
}

void SnesCoreAccess::set_pads(const Pads &p)
{
    for (int i = 0; i < int(p.size()); i++)
        MovieSetJoypad(i, p[i]);
}

void SnesCoreAccess::begin_unseen()
{
    if (unseen_now)
        return;
    before_unseen.render = IPPU.RenderThisFrame;
    before_unseen.info_timeout = GFX.InfoStringTimeout;
    before_unseen.reboots = reset_watch::count;
    before_unseen.reads = S9xWramReads;
    before_unseen.writes = S9xWramWrites;
    S9xWramReads = S9xWramWrites = nullptr;
    unseen_now = true;
}

void SnesCoreAccess::run_unseen_frame(bool draw)
{
    // The front end's S9xSyncSpeed sets it again after each frame; a drawn frame isn't shown
    // (the front end's S9xDeinitUpdate checks unseen()).
    IPPU.RenderThisFrame = draw ? TRUE : FALSE;
    S9xMainLoop();
}

void SnesCoreAccess::end_unseen()
{
    if (!unseen_now)
        return;
    IPPU.RenderThisFrame = before_unseen.render;
    GFX.InfoStringTimeout = before_unseen.info_timeout;
    reset_watch::count = before_unseen.reboots;
    S9xWramReads = before_unseen.reads;
    S9xWramWrites = before_unseen.writes;
    unseen_now = false;
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

RomOffsets SnesCoreAccess::rom_offsets()
{
    RomOffsets out;
    if (!Memory.ROM || !Memory.CalculatedSize)
        return out;
    out.blocks.assign(0x1000000 / RomOffsets::kBlock, RomOffsets::kNone);
    for (uint32 block = 0; block < out.blocks.size(); block++)
        out.blocks[block] = rom_trace::rom_offset_of(block * RomOffsets::kBlock);
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

namespace
{
constexpr uint8_t kTrailerTag[4] = { 'S', 'X', 'T', '1' };
constexpr size_t kTrailer = 3 + sizeof kTrailerTag; // RangeTimeOver, GFX.EndY (2), the tag
} // namespace

bool freeze_state(std::vector<uint8_t> &state)
{
    size_t size = S9xFreezeSize();
    state.resize(size + kTrailer); // keeps its memory when the size is the same, as it always is
    if (!S9xFreezeGameMem(state.data(), uint32(size)))
    {
        state.clear();
        return false;
    }
    uint8_t *t = state.data() + size;
    t[0] = PPU.RangeTimeOver;
    t[1] = uint8_t(GFX.EndY & 0xFF);
    t[2] = uint8_t(GFX.EndY >> 8);
    std::copy(std::begin(kTrailerTag), std::end(kTrailerTag), t + 3);
    return true;
}

bool thaw_state(const std::vector<uint8_t> &state)
{
    size_t size = state.size();
    bool trailer = size > kTrailer && std::equal(std::begin(kTrailerTag), std::end(kTrailerTag), state.end() - 4);
    if (trailer)
        size -= kTrailer;
    if (size == 0 || S9xUnfreezeGameMem(state.data(), uint32(size)) != SUCCESS)
        return false;
    if (trailer)
    {
        const uint8_t *t = state.data() + size;
        PPU.RangeTimeOver = t[0];
        GFX.EndY = uint32(t[1] | t[2] << 8);
    }
    // The CPU flags come back from the state; the core's own stop switch (set by a deadlock)
    // doesn't. Left set, every run after a deadlocked one looked dead.
    Settings.StopEmulation = FALSE;
    return true;
}

std::vector<uint8_t> SnesCoreAccess::save_state()
{
    std::vector<uint8_t> state;
    freeze_state(state);
    return state;
}

bool SnesCoreAccess::save_state_into(std::vector<uint8_t> &state)
{
    return freeze_state(state);
}

bool SnesCoreAccess::load_state(const std::vector<uint8_t> &state)
{
    if (!thaw_state(state))
        return false;
    loads++;
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

bool SnesCoreAccess::picture(Picture &out, bool thumbnail)
{
    int w = IPPU.RenderedScreenWidth, h = IPPU.RenderedScreenHeight;
    if (!GFX.Screen || w <= 0 || h <= 0)
        return false;
    auto rgb = [](uint16 p, uint8_t *to) {
        uint32 r, g, b;
        DECOMPOSE_PIXEL(p, r, g, b);
        to[0] = uint8_t(r << 3 | r >> 2);
        to[1] = uint8_t(g << 3 | g >> 2);
        to[2] = uint8_t(b << 3 | b >> 2);
    };
    if (!thumbnail)
    {
        out.w = uint16_t(w);
        out.h = uint16_t(h);
        out.rgb.resize(size_t(w) * h * 3);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                rgb(GFX.Screen[y * GFX.RealPPL + x], &out.rgb[(size_t(y) * w + x) * 3]);
        return true;
    }
    // Blocks of the visible 224 lines (or 448 interlaced), 4 or 8 pixels wide.
    int bw = std::max(1, w / kThumbW), bh = std::max(1, std::min(h, h >= 448 ? 448 : 224) / kThumbH);
    out.w = kThumbW;
    out.h = kThumbH;
    out.rgb.assign(size_t(kThumbW) * kThumbH * 3, 0);
    for (int ty = 0; ty < kThumbH; ty++)
        for (int tx = 0; tx < kThumbW; tx++)
        {
            uint32_t sum[3] = {};
            int n = 0;
            for (int y = ty * bh; y < (ty + 1) * bh && y < h; y++)
                for (int x = tx * bw; x < (tx + 1) * bw && x < w; x++)
                {
                    uint8_t c[3];
                    rgb(GFX.Screen[y * GFX.RealPPL + x], c);
                    sum[0] += c[0];
                    sum[1] += c[1];
                    sum[2] += c[2];
                    n++;
                }
            uint8_t *to = &out.rgb[(size_t(ty) * kThumbW + tx) * 3];
            for (int k = 0; k < 3 && n; k++)
                to[k] = uint8_t(sum[k] / n);
        }
    return true;
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

bool RomTracer::start(CodeMap &map, JumpLog *jumps, CallLog *calls)
{
    if (rom_trace::installed || !Memory.ROM || map.size() != Memory.CalculatedSize || map.empty())
        return false;
    rom_trace::flags = map.data();
    rom_trace::jumps = jumps;
    rom_trace::calls = calls;
    rom_trace::blind_after_landing = false;
    rom_trace::seen_after_landing = 0;
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
    rom_trace::jumps = nullptr;
    rom_trace::calls = nullptr;
    rom_trace::derails = nullptr;
    paused_jumps = nullptr;
    paused_calls = nullptr;
    mine = false;
}

void RomTracer::note_jumps(bool on)
{
    if (!mine)
        return;
    if (!on)
    {
        if (rom_trace::jumps)
            paused_jumps = std::exchange(rom_trace::jumps, nullptr);
        if (rom_trace::calls)
            paused_calls = std::exchange(rom_trace::calls, nullptr);
    }
    else
    {
        if (paused_jumps)
            rom_trace::jumps = std::exchange(paused_jumps, nullptr);
        if (paused_calls)
            rom_trace::calls = std::exchange(paused_calls, nullptr);
    }
}

void RomTracer::watch_derails(DerailWatch *watch)
{
    if (!mine)
        return;
    if (rom_trace::derails)
        take_derail(); // marks back on
    rom_trace::derails = watch;
    if (watch)
        watch->clear();
}

std::optional<DerailWatch::Note> RomTracer::take_derail()
{
    if (!mine || !rom_trace::derails)
        return std::nullopt;
    auto note = rom_trace::derails->take();
    S9xRomFlags = rom_trace::flags; // the core marks data and DMA again
    return note;
}

void RomTracer::set_blind_after_landing(bool on)
{
    if (mine)
        rom_trace::blind_after_landing = on;
}

uint64_t RomTracer::seen_after_landing() const
{
    return mine ? rom_trace::seen_after_landing : 0;
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
