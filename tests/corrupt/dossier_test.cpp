#include "doctest/doctest.h"

#include "corrupt/dossier.h"

#include <initializer_list>

using namespace corrupt;

namespace
{

// A 64 KB LoROM-like game: offset 0 is $00:8000. A main loop dispatches through a two-entry
// jump table and calls a named "player" function; the table's targets touch the same RAM.
struct Game
{
    std::vector<uint8_t> rom = std::vector<uint8_t>(0x10000, 0xFF);
    std::vector<uint8_t> flags = std::vector<uint8_t>(0x10000, 0);
    DossierInput in;

    void put(uint32_t at, std::initializer_list<uint8_t> bytes)
    {
        uint32_t i = at;
        for (auto b : bytes)
        {
            rom[i] = b;
            flags[i] |= i == at ? codeflag::Opcode : codeflag::Operand;
            i++;
        }
    }
    void function(uint32_t entry, uint64_t frames)
    {
        Function f;
        f.entry = entry;
        f.frames = frames;
        f.calls = uint32_t(frames);
        f.callers = 1;
        in.functions.push_back(f);
    }

    Game()
    {
        for (uint32_t c = 0; c < 16; c++)
            in.where.chunks.push_back(((c / 8) << 16) | (0x8000 + (c % 8) * 0x1000));
        in.offsets.blocks.assign(4096, RomOffsets::kNone);
        for (uint32_t b = 0; b < 8; b++)
        {
            in.offsets.blocks[0x008 + b] = b * 0x1000;
            in.offsets.blocks[0x018 + b] = 0x8000 + b * 0x1000;
        }
        // Main loop, $00:8000.
        put(0x00, { 0xA5, 0x10 });       // LDA $10        (the mode)
        put(0x02, { 0x0A });             // ASL A
        put(0x03, { 0xAA });             // TAX
        put(0x04, { 0xFC, 0x00, 0x81 }); // JSR ($8100,X)
        put(0x07, { 0x20, 0x00, 0x82 }); // JSR $8200
        put(0x0A, { 0x8D, 0x00, 0x21 }); // STA $2100
        put(0x0D, { 0x60 });             // RTS
        // The table, $00:8100: $8300, $8400.
        rom[0x100] = 0x00, rom[0x101] = 0x83, rom[0x102] = 0x00, rom[0x103] = 0x84;
        for (int i = 0; i < 4; i++)
            flags[0x100 + i] |= codeflag::Data;
        // Player, $00:8200.
        put(0x200, { 0xAF, 0x00, 0x0D, 0x7E }); // LDA $7E0D00
        put(0x204, { 0xEE, 0x01, 0x0D });       // INC $0D01
        put(0x207, { 0xBD, 0x00, 0x90 });       // LDA $9000,X (a ROM table)
        put(0x20A, { 0x8F, 0x00, 0x00, 0x7F }); // STA $7F0000
        put(0x20E, { 0x60 });                   // RTS
        flags[0x1000] |= codeflag::Data;
        // The table's targets.
        put(0x300, { 0x64, 0x10 }); // STZ $10
        put(0x302, { 0x60 });
        put(0x400, { 0xA5, 0x10 }); // LDA $10
        put(0x402, { 0x60 });
        // A quiet function nobody named.
        put(0x500, { 0xEA }); // NOP
        put(0x501, { 0x60 });

        put(0x6F0, { 0xEA });       // NOP
        put(0x6F1, { 0x60 });       // RTS
        put(0x700, { 0x80, 0xEE }); // BRA $86F0
        in.names.others[0x700] = { { "erbl", "tail" } };
        function(0x700, 3);
        function(0x000, 600);
        function(0x200, 500);
        function(0x300, 100);
        function(0x400, 50);
        function(0x500, 1);
        in.calls.add({ 0xF000, 0x000 }, 0x20, 600); // from nowhere listed
        in.calls.add({ 0x007, 0x200 }, 0x20, 500);
        JumpTable t;
        t.site = 0x04;
        t.opcode = 0xFC;
        t.entries = { { 0x100, 0x008300, 7, true }, { 0x102, 0x008400, 0, false } };
        in.tables = { t };
        in.names.others[0x200] = { { "erbl", "player" } };
        in.title = "TEST GAME";
        in.sources = { "calls.erbl.json" };
        in.rom = rom;
        in.flags = flags;
    }
};

} // namespace

TEST_SUITE("dossier")
{
    TEST_CASE("an operand's memory: RAM, hardware, ROM data, read or written, and what can't be told")
    {
        Game g;
        auto at = [&](uint32_t offset, int length) { return operand_access(g.rom, offset, length, 0x00); };
        auto mode = at(0x00, 2);
        REQUIRE(mode);
        CHECK(mode->kind == AccessKind::Ram);
        CHECK(mode->address == 0x7E0010);
        CHECK(mode->read);
        CHECK_FALSE(mode->write);
        CHECK(mode->direct); // D = 0 assumed
        auto screen = at(0x0A, 3);
        REQUIRE(screen);
        CHECK(screen->kind == AccessKind::Hardware);
        CHECK(screen->address == 0x2100);
        CHECK(screen->write);
        auto inc = at(0x204, 3);
        REQUIRE(inc);
        CHECK(inc->address == 0x7E0D01); // the low mirror is work RAM
        CHECK((inc->read && inc->write));
        auto table = at(0x207, 3);
        REQUIRE(table);
        CHECK(table->kind == AccessKind::Rom);
        CHECK(table->address == 0x009000);
        CHECK(table->indexed);
        auto far = at(0x20A, 4);
        REQUIRE(far);
        CHECK(far->address == 0x7F0000);
        CHECK(far->write);
        CHECK_FALSE(at(0x04, 3)); // a call touches nothing
        CHECK_FALSE(at(0x07, 3));
        CHECK_FALSE(at(0x02, 1));
        g.put(0x600, { 0xB1, 0x20 }); // LDA ($20),Y: the pointer is read
        auto pointer = at(0x600, 2);
        REQUIRE(pointer);
        CHECK(pointer->address == 0x7E0020);
        CHECK(pointer->read);
        CHECK(pointer->indexed);
        g.put(0x610, { 0xAF, 0x00, 0x00, 0x70 }); // LDA $700000: save RAM
        REQUIRE(at(0x610, 4));
        CHECK(at(0x610, 4)->kind == AccessKind::Save);
    }

    TEST_CASE("registers have names")
    {
        CHECK(register_name(0x2100).find("INIDISP") == 0);
        CHECK(register_name(0x420B).find("MDMAEN") == 0);
        CHECK(register_name(0x4332) == "DMA3 A1TL A-bus addr");
        CHECK(register_name(0x4218).find("JOY1L") == 0);
        CHECK(register_name(0x2200).empty());
    }

    TEST_CASE("the dossier: functions with callers, callees, memory and a listing; tables; the RAM index")
    {
        Game g;
        g.in.max_functions = 2;
        g.in.max_instructions = 2;
        auto text = make_dossier(g.in);
        auto has = [&](const std::string &s) {
            INFO(s);
            CHECK(text.find(s) != std::string::npos);
        };
        has("# Game dossier: TEST GAME");
        has("- From: calls.erbl.json");
        has("The direct page is taken as $0000");
        // Busiest first; the named one and the table's targets are in, the quiet one isn't.
        has("### $00:8000 · ROM 0x000000\n");
        has("### $00:8200 \"player (erbl)\" · ROM 0x000200");
        has("### $00:8300 · ROM 0x000300\n");
        has("### $00:8400 · ROM 0x000400\n");
        CHECK(text.find("### $00:8500") == std::string::npos);
        CHECK(text.find("### $00:8000") < text.find("### $00:8200"));
        // Who calls whom.
        has("- Calls: $00:8200 \"player (erbl)\".");
        has("- Called from: $00:8000.");
        has("- Dispatches through the jump table at $00:8100 (2 entries");
        // Memory, grouped.
        has("- RAM: $7E:0010 r.");
        has("- Hardware: $2100 INIDISP screen on/brightness w.");
        has("- RAM: $7E:0D00 r, $7E:0D01 rw, $7F:0000 w.");
        has("- ROM data read at: $00:9000.");
        // The listing, notes on calls and registers, cut.
        has("$00:8007  JSR $8200    ; $00:8200 \"player (erbl)\"");
        has("$00:8002  ASL A\n; ... 1 instruction left out\n$00:8004  JSR ($8100,X)"); // past the cut: calls,
        has("$00:800A  STA $2100    ; INIDISP");                                         // registers and
        has("$00:800D  RTS");                                                             // returns stay
        // Code before the entry (a branch back) comes after the entry's own, marked.
        has("$00:8700  BRA $86F0\n; --- before the entry, reached by a jump:\n$00:86F0  NOP");
        has("- Reached through jump tables: entry 0 of the table at $00:8100 (from $00:8004); jumped to 7 times.");
        // Jump tables.
        has("### Table at $00:8100, dispatched from $00:8004 in $00:8000");
        has("- 0: $00:8300, jumped through 7 times");
        has("- 1: $00:8400, likely (not seen used)");
        // The RAM index: $10 read by two, written by one.
        has("- $7E:0010: read by 2 ($00:8000, $00:8400); written by 1 ($00:8300)");
    }
}
