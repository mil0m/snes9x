#pragma once
#include "code_map.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// Instant revert (roadmap #51): noticing, as it happens, the CPU landing somewhere that
// can't be code right after a changed ROM byte ran. The tracer (core_access.cpp) feeds
// every instruction to a DerailWatch; between frames, InstantRevert (instant_revert.h)
// replays the frame without the culprits. Plan: docs/corruption/2026-11-01-safety-plan.md.
namespace corrupt
{

enum class Landing : uint8_t
{
    None,
    OpenBus,        // open bus or I/O registers
    MidInstruction, // a byte known only as an operand
    Brk,            // $00 where nothing ran before
    Stop,           // STP ($DB)
    Wdm,            // WDM ($42), which no game uses
};

const char *landing_text(Landing l); // "jumped into the middle of an instruction"
const char *landing_name(Landing l); // "mid_instruction", for the journal

// What an instruction about to start says about where the CPU went. `plain`: PC is in
// ROM or RAM (not open bus or I/O). `in_rom`: PC is in the ROM, and `flags` are its first
// byte's code map flags (Changed included). `opcode`: the byte there.
constexpr Landing landing(bool plain, bool in_rom, uint8_t flags, uint8_t opcode)
{
    if (!plain)
        return Landing::OpenBus;
    if (in_rom && !(flags & codeflag::Opcode))
    {
        // (Known only as a DMA source isn't enough: FE4 DMAs code to RAM and runs it from ROM.)
        if (flags & codeflag::Operand)
            return Landing::MidInstruction;
    }
    // An opcode the map saw run here and that is still the one that ran is the game's.
    bool known = in_rom && (flags & codeflag::Opcode) && !(flags & codeflag::Changed);
    if (known)
        return Landing::None;
    switch (opcode)
    {
    case 0x00: return Landing::Brk;
    case 0xDB: return Landing::Stop;
    case 0x42: return Landing::Wdm;
    default: return Landing::None;
    }
}

// Fed by the tracer, one call per instruction, on the emulation thread. A window opens for
// kWindow instructions whenever a changed byte is used (run, or read as an indirect jump's
// pointer); a bad landing inside it is noted once, with the changed bytes used in the
// window as culprits, and then the watch stays quiet until the note is taken.
class DerailWatch
{
  public:
    static constexpr uint32_t kWindow = 64; // instructions
    static constexpr size_t kRing = 8;      // changed bytes remembered

    struct Note
    {
        Landing kind = Landing::None;
        uint32_t pc = 0;                // 24-bit bank:address of the landing
        std::vector<uint32_t> culprits; // ROM offsets, newest first, each once
    };

    void step() { ++count; }
    void used(uint32_t offset)
    {
        ring[next++ % kRing] = { offset, count };
        if (!note && !held_open)
            open_end = count + kWindow + 1;
    }
    // Whether a landing now would be noted. One comparison: it runs for every instruction.
    bool open() const { return count < open_end; }
    // For measuring false alarms (corrupt-smoke --derail-fp): every instruction is in the
    // window, so on a clean game every note is one the game's own code would have caused.
    void hold_open(bool on)
    {
        held_open = on;
        open_end = on && !note ? UINT64_MAX : 0;
    }
    void land(Landing kind, uint32_t pc);
    bool derailed() const { return note.has_value(); }

    // Between frames: the note, if any. Taking it forgets the window too, since its
    // culprits are about to be dealt with.
    std::optional<Note> take();
    void clear(); // forget everything (a game loaded, a run started)

  private:
    struct Use
    {
        uint32_t offset = 0;
        uint64_t at = 0;
    };
    std::array<Use, kRing> ring{};
    size_t next = 0;
    uint64_t count = 0;
    uint64_t open_end = 0; // the window is open while count < open_end; 0 when closed
    bool held_open = false;
    std::optional<Note> note;
};

} // namespace corrupt
