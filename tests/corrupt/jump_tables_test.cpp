#include "doctest/doctest.h"
#include "corrupt/code_map.h"
#include "corrupt/jump_tables.h"
#include "corrupt/library.h"

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace corrupt;
namespace cf = codeflag;
namespace fs = std::filesystem;

namespace
{

constexpr size_t kRomSize = 0x10000; // two LoROM banks

// LoROM: $00-$7D and $80-$FF, $8000-$FFFF, 32 KB a bank, mirrored to fill.
RomOffsets lorom(size_t size)
{
    RomOffsets out;
    out.blocks.assign(4096, RomOffsets::kNone);
    for (uint32_t block = 0; block < 4096; block++)
    {
        uint32_t address = block * RomOffsets::kBlock;
        uint32_t bank = address >> 16;
        if ((address & 0xFFFF) < 0x8000 || bank == 0x7E || bank == 0x7F)
            continue;
        out.blocks[block] = uint32_t(((bank & 0x7F) * 0x8000 + (address & 0x7FFF)) % size);
    }
    return out;
}

// A fake game: ROM bytes, the code map's flags, and a LoROM layout.
struct Game
{
    std::vector<uint8_t> rom = std::vector<uint8_t>(kRomSize, 0xEA);
    std::vector<uint8_t> flags = std::vector<uint8_t>(kRomSize, 0);
    RomOffsets offsets = lorom(kRomSize);

    void put16(uint32_t offset, uint16_t v)
    {
        rom[offset] = uint8_t(v);
        rom[offset + 1] = uint8_t(v >> 8);
    }
    void code(uint32_t offset, uint8_t opcode = 0xE6) // an instruction the game ran
    {
        rom[offset] = opcode;
        flags[offset] |= cf::Opcode;
        flags[offset + 1] |= cf::Operand;
    }
    void data(uint32_t offset, int n = 2)
    {
        for (int i = 0; i < n; i++)
            flags[offset + i] |= cf::Data;
    }
    JumpFindings find(const JumpLog &log) const { return find_jump_tables(log, rom, flags, offsets); }
};

const JumpTable *table_at(const JumpFindings &f, uint32_t site)
{
    for (auto &t : f.tables)
        if (t.site == site)
            return &t;
    return nullptr;
}

} // namespace

TEST_SUITE("jump log")
{
    TEST_CASE("it counts each jump and grows only with new ones")
    {
        JumpLog log;
        log.note(0x50, jumpop::JsrIndexed, 0x100, 0x008200);
        log.note(0x50, jumpop::JsrIndexed, 0x100, 0x008200);
        CHECK(log.size() == 1);
        auto print = log.fingerprint();
        log.note(0x50, jumpop::JsrIndexed, 0x102, 0x008210);
        CHECK(log.size() == 2);
        CHECK(log.fingerprint() > print);
        auto &seen = log.all().at(JumpKey{ 0x50, 0x100, 0x008200 });
        CHECK(seen.hits == 2);
        CHECK(seen.opcode == jumpop::JsrIndexed);
    }

    TEST_CASE("merging adds the hits up")
    {
        JumpLog a, b;
        a.note(0x50, jumpop::JmpPointer, kNoEntry, 0x008300);
        b.note(0x50, jumpop::JmpPointer, kNoEntry, 0x008300);
        b.note(0x50, jumpop::JmpPointer, kNoEntry, 0x008300);
        b.note(0x60, jumpop::JmlPointer, kNoEntry, 0xC01234);
        a.merge(b);
        CHECK(a.size() == 2);
        CHECK(a.all().at(JumpKey{ 0x50, kNoEntry, 0x008300 }).hits == 3);
        CHECK(a.all().at(JumpKey{ 0x60, kNoEntry, 0xC01234 }).opcode == jumpop::JmlPointer);
    }

    TEST_CASE("the file keeps every jump, and refuses another ROM size or junk")
    {
        JumpLog log;
        log.note(0x050160, jumpop::JmpIndexed, 0x0501A0, 0xC51234);
        log.note(0x050160, jumpop::JmpIndexed, 0x0501A0, 0xC51234);
        log.note(0x009A8E, jumpop::JmpPointer, kNoEntry, 0x00A000);
        auto file = encode_jump_log(log, 0x300000);
        CHECK(file.find("\"0x050160\"") != std::string::npos); // readable offsets
        CHECK(file.find("\"7C\"") != std::string::npos);
        std::string error;
        auto back = decode_jump_log(file, 0x300000, error);
        REQUIRE(back);
        CHECK(error.empty());
        CHECK(back->size() == 2);
        CHECK(back->all().at(JumpKey{ 0x050160, 0x0501A0, 0xC51234 }).hits == 2);
        CHECK(back->all().at(JumpKey{ 0x009A8E, kNoEntry, 0x00A000 }).opcode == jumpop::JmpPointer);

        CHECK_FALSE(decode_jump_log(file, 0x200000, error));
        CHECK(error.find("size") != std::string::npos);
        CHECK_FALSE(decode_jump_log("{\"format\": \"something else\"}", 0x300000, error));
        CHECK_FALSE(decode_jump_log("not json", 0x300000, error));
        // An entry with a bad field is an error, not a crash.
        CHECK_FALSE(decode_jump_log(R"({"format":"snes9x-jumps","version":1,"rom_size":16,"jumps":[{"site":5}]})", 16, error));
    }
}

TEST_SUITE("jump tables")
{
    TEST_CASE("a direct table: the entries used, and likely ones on either side")
    {
        Game g;
        // JSR ($8100,X) at $00:8050; routines at $8200, $8210, $8220 (ran), $8230 (never ran).
        g.code(0x50, jumpop::JsrIndexed);
        g.put16(0x0FE, 0x0040); // before the table: $00:0040 is RAM, not a routine
        g.put16(0x100, 0x8200);
        g.put16(0x102, 0x8210);
        g.put16(0x104, 0x8220);
        g.put16(0x106, 0x8230);
        g.put16(0x108, 0x9000); // known data: the table is over
        g.data(0x100, 6);
        g.code(0x200);
        g.code(0x210);
        g.code(0x220);
        g.rom[0x230] = 0xA9; // never ran: nothing says it's a routine
        g.data(0x1000);
        JumpLog log;
        log.note(0x50, jumpop::JsrIndexed, 0x100, 0x008200);
        log.note(0x50, jumpop::JsrIndexed, 0x102, 0x008210);
        log.note(0x50, jumpop::JsrIndexed, 0x102, 0x008210);

        auto f = g.find(log);
        REQUIRE(f.tables.size() == 1);
        auto &t = f.tables[0];
        CHECK(t.site == 0x50);
        CHECK(t.opcode == jumpop::JsrIndexed);
        CHECK(t.kind == JumpKind::Direct);
        CHECK(t.stride == 2);
        REQUIRE(t.entries.size() == 3);
        CHECK(t.seen() == 2);
        CHECK(t.likely() == 1); // $8220 ran (from somewhere else); $8230 never did
        CHECK(t.entries[0].offset == 0x100);
        CHECK(t.entries[0].seen);
        CHECK(t.entries[1].hits == 2);
        CHECK(t.entries[2].target == 0x008220);
        CHECK_FALSE(t.entries[2].seen);
        CHECK(t.first() == 0x100);
        CHECK(t.last() == 0x105);
        CHECK(t.bytes_for(0x008210) == std::vector<uint8_t>{ 0x10, 0x82 });
    }

    TEST_CASE("likely entries stop at code, data, mid-instruction, code that never ran and other banks")
    {
        Game g;
        g.code(0x50, jumpop::JmpIndexed);
        g.put16(0x100, 0x8200);
        g.data(0x100);
        g.code(0x200);
        JumpLog log;
        log.note(0x50, jumpop::JmpIndexed, 0x100, 0x008200);

        SUBCASE("the next entry's bytes are code")
        {
            g.put16(0x102, 0x8200);
            g.flags[0x102] |= cf::Opcode;
        }
        SUBCASE("it points at data")
        {
            g.put16(0x102, 0x8300);
            g.data(0x300);
        }
        SUBCASE("it points inside an instruction")
        {
            g.put16(0x102, 0x8201);
        }
        SUBCASE("it points at code that never ran")
        {
            g.put16(0x102, 0x8300);
            g.rom[0x300] = 0xA9;
        }
        SUBCASE("it points at another bank")
        {
            g.put16(0x102, 0x0300); // $00:0300 is RAM
        }
        g.put16(0x0FE, 0x0010); // and RAM before it
        auto f = g.find(log);
        REQUIRE(f.tables.size() == 1);
        CHECK(f.tables[0].entries.size() == 1);
    }

    TEST_CASE("through RAM: the entry is the one data-read spot holding the target")
    {
        Game g;
        // JMP ($0014) at $00:8060 went to $00:8400 and $00:8410; the game read its table at $00:9200.
        g.code(0x60, jumpop::JmpPointer);
        g.put16(0x1200, 0x8400);
        g.put16(0x1202, 0x8410);
        g.data(0x1200, 4);
        g.put16(0x11FE, 0x0010); // RAM on either side: the table is two entries
        g.put16(0x1204, 0x0010);
        g.code(0x400);
        g.code(0x410);
        g.put16(0x1500, 0x8400); // the same value again, but nothing read it: doesn't count
        JumpLog log;
        log.note(0x60, jumpop::JmpPointer, kNoEntry, 0x008400);
        log.note(0x60, jumpop::JmpPointer, kNoEntry, 0x008410);
        log.note(0x60, jumpop::JmpPointer, kNoEntry, 0x008500); // built in code: no table has it

        auto f = g.find(log);
        auto t = table_at(f, 0x60);
        REQUIRE(t);
        CHECK(t->kind == JumpKind::ThroughRam);
        CHECK(t->stride == 2);
        CHECK(t->seen() == 2);
        CHECK(t->first() == 0x1200);
        CHECK(f.pointer_jumps == 1);
        CHECK(f.ambiguous == 0);
    }

    TEST_CASE("through RAM, two data-read spots with the value: ambiguous, no entry")
    {
        Game g;
        g.code(0x60, jumpop::JmpPointer);
        g.put16(0x1200, 0x8400);
        g.put16(0x1800, 0x8400);
        g.data(0x1200);
        g.data(0x1800);
        g.code(0x400);
        JumpLog log;
        log.note(0x60, jumpop::JmpPointer, kNoEntry, 0x008400);
        auto f = g.find(log);
        CHECK(f.tables.empty());
        CHECK(f.ambiguous == 1);
    }

    TEST_CASE("a long pointer through RAM is matched with its bank, 3 bytes an entry")
    {
        Game g;
        g.code(0x70, jumpop::JmlPointer);
        // $81:8400 and $81:8410 (offsets 0xC400, 0xC410), table at offset 0x1300.
        uint8_t table[] = { 0x00, 0x84, 0x81, 0x10, 0x84, 0x81 };
        for (int i = 0; i < 6; i++)
            g.rom[0x1300 + i] = table[i];
        g.data(0x1300, 6);
        g.put16(0x1400, 0x8400); // the low 2 bytes alone, read as data elsewhere: the bank decides
        g.rom[0x1402] = 0x80;
        g.data(0x1400, 3);
        g.code(0xC400);
        g.code(0xC410);
        JumpLog log;
        log.note(0x70, jumpop::JmlPointer, kNoEntry, 0x818400);
        log.note(0x70, jumpop::JmlPointer, kNoEntry, 0x818410);
        auto f = g.find(log);
        auto t = table_at(f, 0x70);
        REQUIRE(t);
        CHECK(t->stride == 3);
        CHECK(t->seen() == 2);
        CHECK(t->first() == 0x1300);
        CHECK(t->bytes_for(0x818410) == std::vector<uint8_t>{ 0x10, 0x84, 0x81 });
    }

    TEST_CASE("likely entries fill the gaps between used ones, and skip what can't be a routine")
    {
        Game g;
        g.code(0x50, jumpop::JmpIndexed);
        g.put16(0x0FE, 0x0010);
        g.put16(0x100, 0x8200); // used
        g.put16(0x102, 0x8210); // ran from elsewhere: likely
        g.put16(0x104, 0x8300); // never ran: skipped, the walk goes on
        g.put16(0x106, 0x8220); // used
        g.put16(0x108, 0x0010);
        g.data(0x100, 8);
        g.code(0x200);
        g.code(0x210);
        g.code(0x220);
        JumpLog log;
        log.note(0x50, jumpop::JmpIndexed, 0x100, 0x008200);
        log.note(0x50, jumpop::JmpIndexed, 0x106, 0x008220);
        auto f = g.find(log);
        REQUIRE(f.tables.size() == 1);
        CHECK(f.tables[0].seen() == 2);
        CHECK(f.tables[0].likely() == 1);
        CHECK(f.tables[0].entries[1].offset == 0x102);
    }

    TEST_CASE("an ambiguous value is settled by the one spot in line with the site's other entries")
    {
        Game g;
        g.code(0x60, jumpop::JmpPointer);
        g.put16(0x11FE, 0x0010);
        g.put16(0x1200, 0x8400);
        g.put16(0x1202, 0x8410);
        g.put16(0x1204, 0x0010);
        g.data(0x1200, 4);
        g.put16(0x3000, 0x8410); // the same value read as data far away
        g.data(0x3000);
        g.code(0x400);
        g.code(0x410);
        JumpLog log;
        log.note(0x60, jumpop::JmpPointer, kNoEntry, 0x008400); // unique
        log.note(0x60, jumpop::JmpPointer, kNoEntry, 0x008410); // two spots; 0x1202 lines up with 0x1200
        auto f = g.find(log);
        REQUIRE(f.tables.size() == 1);
        CHECK(f.tables[0].seen() == 2);
        CHECK(f.tables[0].entries[1].offset == 0x1202);
        CHECK(f.ambiguous == 0);
    }

    TEST_CASE("entries of one site far apart are two tables")
    {
        Game g;
        g.code(0x50, jumpop::JmpIndexed);
        g.put16(0x100, 0x8200);
        g.put16(0x3000, 0x8210);
        g.data(0x100);
        g.data(0x3000);
        g.code(0x200);
        g.code(0x210);
        g.put16(0x0FE, 0x0010);
        g.put16(0x102, 0x0010);
        g.put16(0x2FFE, 0x0010);
        g.put16(0x3002, 0x0010);
        JumpLog log;
        log.note(0x50, jumpop::JmpIndexed, 0x100, 0x008200);
        log.note(0x50, jumpop::JmpIndexed, 0x3000, 0x008210);
        auto f = g.find(log);
        CHECK(f.tables.size() == 2);
    }

    TEST_CASE("two sites using the same entries are two tables")
    {
        Game g;
        g.code(0x50, jumpop::JsrIndexed);
        g.code(0x58, jumpop::JmpIndexed);
        g.put16(0x100, 0x8200);
        g.put16(0x102, 0x8210);
        g.data(0x100, 4);
        g.code(0x200);
        g.code(0x210);
        g.put16(0x0FE, 0x0010);
        g.put16(0x104, 0x0010);
        JumpLog log;
        log.note(0x50, jumpop::JsrIndexed, 0x100, 0x008200);
        log.note(0x58, jumpop::JmpIndexed, 0x102, 0x008210);
        auto f = g.find(log);
        REQUIRE(f.tables.size() == 2);
        CHECK(table_at(f, 0x50)->seen() == 1);
        CHECK(table_at(f, 0x58)->seen() == 1);
    }

    TEST_CASE("nothing recorded, nothing found")
    {
        Game g;
        auto f = g.find(JumpLog{});
        CHECK(f.tables.empty());
        CHECK(f.pointer_jumps == 0);
    }
}

TEST_SUITE("jump logs kept")
{
    TEST_CASE("the keeper records into mine, combines with everyone's, and saves once a minute while it grows")
    {
        JumpLogs loaded;
        loaded.mine.note(0x10, jumpop::JmpIndexed, 0x100, 0x008000);
        loaded.others.note(0x20, jumpop::JmpIndexed, 0x200, 0x008010);
        JumpLogKeeper keeper;
        keeper.reset(std::move(loaded));
        CHECK(keeper.mine().size() == 1);
        CHECK(keeper.combined().size() == 2);
        CHECK_FALSE(keeper.unsaved());
        for (uint64_t i = 0; i < JumpLogKeeper::kSaveEveryFrames; i++)
            keeper.tick();
        CHECK_FALSE(keeper.save_due()); // nothing new
        keeper.mine().note(0x10, jumpop::JmpIndexed, 0x100, 0x008000);
        CHECK_FALSE(keeper.unsaved()); // more hits alone aren't worth a save
        keeper.mine().note(0x30, jumpop::JmpIndexed, 0x300, 0x008020);
        CHECK(keeper.unsaved());
        CHECK_FALSE(keeper.save_due()); // the minute started over
        for (uint64_t i = 0; i < JumpLogKeeper::kSaveEveryFrames; i++)
            keeper.tick();
        CHECK(keeper.save_due());
        keeper.saved();
        CHECK_FALSE(keeper.unsaved());
    }

    TEST_CASE("the library keeps one file per person and loads them all")
    {
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = fs::temp_directory_path() / ("corrupt-jumps-test-" + std::to_string(stamp));
        fs::create_directories(dir);
        {
            Library lib(dir);
            GameId game{ "TEST", "abcdef0123456789" };
            JumpLog mine, erik;
            mine.note(0x10, jumpop::JmpIndexed, 0x100, 0x008000);
            erik.note(0x20, jumpop::JsrIndexed, kNoEntry, 0x008010);
            CHECK(lib.save_jump_log(game, "mikael", mine, 0x1000).empty());
            CHECK(lib.save_jump_log(game, "erik", erik, 0x1000).empty());
            CHECK(lib.jump_log_path(game, "mikael").filename() == "jumps.mikael.json");
            CHECK(lib.save_jump_log(game, "old", erik, 0x800).empty()); // another ROM size
            {
                std::ofstream out(lib.jump_log_path(game, "broken"));
                out << "garbage";
            }
            auto logs = lib.load_jump_logs(game, "mikael", 0x1000);
            CHECK(logs.mine.size() == 1);
            CHECK(logs.others.size() == 1);
            CHECK(logs.others.all().count(JumpKey{ 0x20, kNoEntry, 0x008010 }));
            CHECK(logs.errors.size() == 2);
        }
        fs::remove_all(dir);
    }
}

TEST_SUITE("jump tables shown")
{
    TEST_CASE("every byte of an entry is marked, seen above likely, and sites stand out")
    {
        JumpTable t;
        t.site = 0x50;
        t.stride = 2;
        t.entries = { { 0x100, 0x008200, 3, true }, { 0x102, 0x008210, 0, false } };
        auto marks = jump_marks({ t }, 0x200);
        REQUIRE(marks.size() == 0x200);
        CHECK(marks[0x50] == jumpmark::Site);
        CHECK(marks[0x100] == jumpmark::Seen);
        CHECK(marks[0x101] == jumpmark::Seen);
        CHECK(marks[0x102] == jumpmark::Likely);
        CHECK(marks[0x103] == jumpmark::Likely);
        CHECK(marks[0x104] == 0);
        CHECK(jump_marks({ t }, 0x80)[0x50] == jumpmark::Site); // what doesn't fit is left out
    }

    TEST_CASE("an offset finds its entry, its table, and the tables a site dispatches through")
    {
        JumpTable a, b;
        a.site = 0x50;
        a.entries = { { 0x100, 0x008200, 3, true }, { 0x102, 0x008210, 0, false } };
        b.site = 0x50;
        b.entries = { { 0x300, 0x008300, 1, true } };
        std::vector<JumpTable> tables{ a, b };
        auto at = jump_at(tables, 0x103);
        REQUIRE(at.entry);
        CHECK(at.table == &tables[0]);
        CHECK(at.index == 1);
        CHECK(at.entry->target == 0x008210);
        CHECK_FALSE(jump_at(tables, 0x104).entry);
        auto site = jump_at(tables, 0x50);
        CHECK_FALSE(site.entry);
        CHECK(site.site_tables == 2);
        CHECK(site.site_entries == 3);
    }
}
