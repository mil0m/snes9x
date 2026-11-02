#include "test_rom.h"

#include <cstring>

namespace testrom
{

namespace
{

constexpr size_t kSize = 0x8000; // 32 KB LoROM, bank $00:8000-FFFF

void put(std::vector<uint8_t> &rom, uint16_t addr, std::initializer_list<uint8_t> bytes)
{
    size_t at = addr - 0x8000;
    for (auto b : bytes)
        rom[at++] = b;
}

void put16(std::vector<uint8_t> &rom, uint16_t addr, uint16_t value)
{
    put(rom, addr, { uint8_t(value & 0xFF), uint8_t(value >> 8) });
}

} // namespace

void fix_checksum(std::vector<uint8_t> &rom);

std::vector<uint8_t> build(Crash crash)
{
    std::vector<uint8_t> rom(kSize, 0xEA); // NOP padding

    // reset:
    put(rom, 0x8000, {
        0x78,             // SEI
        0x18,             // CLC
        0xFB,             // XCE        native mode, 8-bit A/X/Y
        0x64, 0x10,       // STZ $10    trigger
        0x64, 0x11,       // STZ $11    frame counter
        0xA9, 0x0F,       // LDA #$0F
        0x8D, 0x00, 0x21, // STA $2100  screen on, full brightness
        0xA9, 0x80,       // LDA #$80
        0x8D, 0x00, 0x42, // STA $4200  NMI on
    });
    // main ($8011):
    put(rom, 0x8011, {
        0xCB,       // WAI        wait for the next frame
        0xA5, 0x10, // LDA $10
        0xC9, 0x42, // CMP #$42
        0xD0, 0xF9, // BNE main
    });
    // crash ($8018):
    switch (crash)
    {
    case Crash::None: put(rom, 0x8018, { 0x80, 0xF7 }); break;                          // BRA main
    case Crash::Stop: put(rom, 0x8018, { 0xDB }); break;                                // STP
    case Crash::NmiOff: put(rom, 0x8018, { 0x9C, 0x00, 0x42, 0x80, 0xFE }); break;      // STZ $4200; BRA *
    case Crash::Reboot: put(rom, 0x8018, { 0x5C, 0x00, 0x80, 0x80 }); break;            // JML $80:8000 (a mirror of reset)
    }
    // nmi ($8030), and a bare RTI for every other vector ($8034):
    put(rom, 0x8030, { 0xE6, 0x11, 0x40 }); // INC $11; RTI
    put(rom, 0x8034, { 0x40 });             // RTI

    // Header.
    const char *title = "CORRUPT TEST ROM     ";
    std::memcpy(&rom[0x7FC0], title, 21);
    rom[0x7FD5] = 0x20; // LoROM
    rom[0x7FD6] = 0x00; // ROM only
    rom[0x7FD7] = 0x05; // 32 KB
    rom[0x7FD8] = 0x00; // no SRAM
    rom[0x7FD9] = 0x01; // North America
    rom[0x7FDA] = 0x00;
    rom[0x7FDB] = 0x00;

    // Vectors: native COP, BRK, ABORT, NMI, -, IRQ; emulation COP, -, ABORT, NMI, RESET, IRQ.
    for (uint16_t v : { 0xFFE4, 0xFFE6, 0xFFE8, 0xFFEE, 0xFFF4, 0xFFF8, 0xFFFE })
        put16(rom, v, 0x8034);
    put16(rom, 0xFFEA, 0x8030); // native NMI
    put16(rom, 0xFFFA, 0x8030); // emulation NMI
    put16(rom, 0xFFFC, 0x8000); // RESET

    fix_checksum(rom);
    return rom;
}

void fix_checksum(std::vector<uint8_t> &rom)
{
    put16(rom, 0xFFDC, 0xFFFF);
    put16(rom, 0xFFDE, 0x0000);
    uint16_t sum = 0;
    for (auto b : rom)
        sum = uint16_t(sum + b);
    put16(rom, 0xFFDC, uint16_t(~sum));
    put16(rom, 0xFFDE, sum);
}

std::vector<uint8_t> build_traced()
{
    auto rom = build(Crash::None);
    put(rom, 0x8030, { 0x4C, 0x40, 0x80 }); // nmi: JMP $8040
    put(rom, 0x8040, {
        0xC2, 0x20,             // REP #$20         16-bit A
        0xA9, 0x34, 0x12,       // LDA #$1234       a 16-bit immediate
        0xAD, 0x00, 0x90,       // LDA $9000        reads $9000-$9001
        0xE2, 0x20,             // SEP #$20
        0xA9, 0x05,             // LDA #$05         an 8-bit immediate
        0xAF, 0x10, 0x90, 0x00, // LDA $00:9010     reads $9010
        0xA9, 0x01,             // LDA #$01         DMA channel 0: one register, to VRAM
        0x8D, 0x00, 0x43,       // STA $4300
        0xA9, 0x18,             // LDA #$18
        0x8D, 0x01, 0x43,       // STA $4301
        0x9C, 0x02, 0x43,       // STZ $4302        from $00:A000
        0xA9, 0xA0,             // LDA #$A0
        0x8D, 0x03, 0x43,       // STA $4303
        0x9C, 0x04, 0x43,       // STZ $4304
        0xA9, 0x20,             // LDA #$20         32 bytes
        0x8D, 0x05, 0x43,       // STA $4305
        0x9C, 0x06, 0x43,       // STZ $4306
        0xA9, 0x01,             // LDA #$01
        0x8D, 0x0B, 0x42,       // STA $420B        go
        0xE6, 0x11,             // INC $11
        0x40,                   // RTI
    });
    for (int i = 0; i < 0x40; i++)
        rom[traced::kTable + i] = uint8_t(0x80 + i);
    for (int i = 0; i < traced::kDmaBytes + 16; i++)
        rom[traced::kDmaSource + i] = uint8_t(i);
    fix_checksum(rom);
    return rom;
}

std::vector<uint8_t> build_rare()
{
    auto rom = build(Crash::None);
    put(rom, 0x8018, {
        0x20, 0x60, 0x80, // JSR $8060
        0x80, 0xF4,       // BRA main
    });
    put(rom, 0x8060, {
        0xE6, 0x12, // INC $12
        0x64, 0x10, // STZ $10
        0x60,       // RTS
    });
    fix_checksum(rom);
    return rom;
}

std::vector<uint8_t> build_dispatch()
{
    auto rom = build(Crash::None);
    put(rom, 0x8030, { 0x4C, 0x40, 0x80 }); // nmi: JMP $8040
    put(rom, 0x8040, {
        0xA5, 0x11,       // LDA $11         frame counter
        0x29, 0x03,       // AND #$03
        0x0A,             // ASL A           entry offset 0, 2, 4, 6
        0xAA,             // TAX
        0xFC, 0x00, 0x91, // JSR ($9100,X)   straight through the ROM table ($8046)
        0xBD, 0x00, 0x92, // LDA $9200,X     copy the other table's entry to $14
        0x85, 0x14,       // STA $14
        0xBD, 0x01, 0x92, // LDA $9201,X
        0x85, 0x15,       // STA $15
        0x6C, 0x14, 0x00, // JMP ($0014)     through RAM ($8053)
        0xE6, 0x11,       // back ($8056): INC $11
        0x40,             // RTI
    });
    for (int i = 0; i < dispatch::kEntries; i++)
    {
        auto r = uint16_t(0x8000 + dispatch::kRoutines + 0x10 * i);
        put(rom, r, { 0xE6, uint8_t(dispatch::kCounts + i), 0x60 }); // INC $20+i; RTS
        put16(rom, uint16_t(0x8000 + dispatch::kTable + 2 * i), r);
        auto s = uint16_t(0x8000 + dispatch::kRamRoutines + 0x10 * i);
        put(rom, s, { 0xE6, uint8_t(dispatch::kRamCounts + i), 0x4C, 0x56, 0x80 }); // INC $30+i; JMP back
        put16(rom, uint16_t(0x8000 + dispatch::kRamTable + 2 * i), s);
    }
    put16(rom, uint16_t(0x8000 + dispatch::kTable - 2), 0x0000);
    put16(rom, uint16_t(0x8000 + dispatch::kTable + 2 * dispatch::kEntries), uint16_t(0x8000 + dispatch::kRoutines));
    put16(rom, uint16_t(0x8000 + dispatch::kTable + 2 * dispatch::kEntries + 2), 0x0000);
    put16(rom, uint16_t(0x8000 + dispatch::kRamTable - 2), 0x0000);
    put16(rom, uint16_t(0x8000 + dispatch::kRamTable + 2 * dispatch::kEntries), 0x0000);
    fix_checksum(rom);
    return rom;
}

std::vector<uint8_t> build_overscan_flip()
{
    auto rom = build(Crash::None);
    put(rom, 0x8011, {
        0xAD, 0x37, 0x21, // LDA $2137     latch the H/V counters
        0xAD, 0x3F, 0x21, // LDA $213F     next $213D read is the low byte
        0xAD, 0x3D, 0x21, // LDA $213D     V, low byte
        0xC9, 0xC8,       // CMP #200
        0x90, 0x0B,       // BCC off
        0xC9, 0xE9,       // CMP #233
        0xB0, 0x07,       // BCS off
        0xA9, 0x04,       // LDA #$04
        0x8D, 0x33, 0x21, // STA $2133     overscan on
        0x80, 0xE8,       // BRA loop
        0x9C, 0x33, 0x21, // off ($8029): STZ $2133
        0x80, 0xE3,       // BRA loop
    });
    fix_checksum(rom);
    return rom;
}

std::vector<uint8_t> build_derail()
{
    auto rom = build(Crash::None);
    put(rom, 0x8011, {
        0xCB,             // WAI
        0x20, 0x60, 0x80, // JSR $8060
        0x80, 0xFA,       // BRA main
    });
    put(rom, 0x8060, {
        0xE6, 0x14,       // INC $14
        0xA2, 0x50,       // LDX #$50
        0xCA,             // DEX        ($8064)
        0xD0, 0xFD,       // BNE $8064
        0x80, 0x00,       // BRA +0     ($8067)
        0xAD, 0x00, 0xDB, // LDA $DB00  ($8069)
        0xE6, 0x13,       // INC $13
        0x60,             // RTS
    });
    fix_checksum(rom);
    return rom;
}

std::vector<uint8_t> build_landing()
{
    auto rom = build(Crash::None);
    put(rom, 0x8011, {
        0xCB,             // WAI          main
        0x20, 0x60, 0x80, // JSR $8060    the branch
        0x20, 0x80, 0x80, // JSR $8080    later in the frame
        0x80, 0xF7,       // BRA main
    });
    put(rom, 0x8060, {
        0x80, 0x00,       // BRA +0       (kBranch)
        0xA9, 0xEA,       // LDA #$EA     (kLanding: a NOP)
        0x60,             // RTS
    });
    put(rom, 0x8080, {
        0xE6, 0x16,       // INC $16      (kInnocent)
        0xE6, 0x12,       // INC $12
        0x60,             // RTS
    });
    fix_checksum(rom);
    return rom;
}

std::vector<uint8_t> build_fuse()
{
    auto rom = build(Crash::None);
    // WRAM powers up as $55s: clear the fuse and counts before NMI goes on.
    put(rom, 0x800E, { 0x4C, 0x50, 0x80 }); // JMP $8050 (instead of STA $4200)
    put(rom, 0x8050, {
        0x64, 0x15,       // STZ $15
        0x64, 0x16,       // STZ $16
        0x64, 0x12,       // STZ $12
        0x8D, 0x00, 0x42, // STA $4200    NMI on (A is still $80)
        0x4C, 0x11, 0x80, // JMP main
    });
    put(rom, 0x8011, {
        0xCB,             // WAI          main
        0x20, 0x70, 0x80, // JSR $8070    tick the fuse
        0xA5, 0x10,       // LDA $10
        0xC9, 0x42,       // CMP #$42
        0xD0, 0xF6,       // BNE main
        0x20, 0x60, 0x80, // JSR $8060    somewhere new
        0x80, 0xF1,       // BRA main
    });
    put(rom, 0x8060, {
        0xA9, 0x00,       // LDA #$00     (kLight)
        0x85, 0x15,       // STA $15
        0xE6, 0x16,       // INC $16      (kInnocent)
        0xE6, 0x12,       // INC $12
        0x64, 0x10,       // STZ $10
        0x60,             // RTS
    });
    put(rom, 0x8070, {
        0xA5, 0x15,       // LDA $15
        0xF0, 0x05,       // BEQ out
        0xC6, 0x15,       // DEC $15
        0xD0, 0x01,       // BNE out
        0xDB,             // STP          ($8078)
        0x60,             // RTS          out ($8079)
    });
    fix_checksum(rom);
    return rom;
}

std::vector<uint8_t> build_calls()
{
    auto rom = build(Crash::None);
    put(rom, 0x8011, {
        0xCB,                   // WAI              main
        0x20, 0x60, 0x80,       // JSR $8060        near
        0x22, 0x70, 0x80, 0x00, // JSL $00:8070     long
        0x4B,                   // PHK
        0x20, 0xA0, 0x80,       // JSR $80A0        mixed, near (PHK makes the RTL come back)
        0x22, 0xA0, 0x80, 0x00, // JSL $00:80A0     mixed, long
        0x20, 0xB0, 0x80,       // JSR $80B0        inline data after it
        0x00,                   //                  the data: a BRK if it ever runs
        0x80, 0xEA,             // BRA main
    });
    put(rom, 0x8060, { 0xE6, 0x20, 0x60 }); // INC $20; RTS
    put(rom, 0x8070, { 0xE6, 0x21, 0x6B }); // INC $21; RTL
    put(rom, 0x80A0, { 0xE6, 0x22, 0x6B }); // INC $22; RTL
    put(rom, 0x80B0, {
        0xC2, 0x20,             // REP #$20
        0x68,                   // PLA              the return address
        0x1A,                   // INC A            past the data byte
        0x48,                   // PHA
        0xE2, 0x20,             // SEP #$20
        0xE6, 0x23,             // INC $23
        0x60,                   // RTS
    });
    fix_checksum(rom);
    return rom;
}

std::vector<uint8_t> build_bodies()
{
    auto rom = build(Crash::None);
    put(rom, 0x8011, {
        0xCB,             // WAI              main
        0x20, 0x60, 0x80, // JSR $8060        step
        0x20, 0xC0, 0x80, // JSR $80C0        other
        0x80, 0xF7,       // BRA main
    });
    put(rom, 0x8060, {
        0xA5, 0x20,       // LDA $20          step: the counter goes up by one
        0x18,             // CLC
        0x69, 0x01,       // ADC #$01
        0x85, 0x20,       // STA $20
        0x29, 0x01,       // AND #$01
        0xF0, 0x02,       // BEQ +2           every other frame
        0xE6, 0x22,       // INC $22
        0x20, 0xA0, 0x80, // JSR $80A0        helper
        0x60,             // RTS
    });
    put(rom, 0x80A0, { 0xE6, 0x21, 0x60 }); // INC $21; RTS   helper
    put(rom, 0x80C0, { 0xE6, 0x23, 0x60 }); // INC $23; RTS   other
    fix_checksum(rom);
    return rom;
}

} // namespace testrom

