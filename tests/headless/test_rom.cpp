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

    // Checksum.
    put16(rom, 0xFFDC, 0xFFFF);
    put16(rom, 0xFFDE, 0x0000);
    uint16_t sum = 0;
    for (auto b : rom)
        sum = uint16_t(sum + b);
    put16(rom, 0xFFDC, uint16_t(~sum));
    put16(rom, 0xFFDE, sum);
    return rom;
}

} // namespace testrom
