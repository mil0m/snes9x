#include "doctest/doctest.h"
#include "corrupt/code_map.h"
#include "corrupt/library.h"

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace corrupt;
namespace fs = std::filesystem;
namespace cf = codeflag;

namespace
{
struct TempDir
{
    fs::path path;
    TempDir()
    {
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("corrupt-codemap-test-" + std::to_string(stamp));
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
};
} // namespace

TEST_SUITE("code map")
{
    TEST_CASE("coverage counts each role")
    {
        CodeMap map(64);
        map.mark(0, cf::Opcode | cf::Op8);
        map.mark(1, cf::Operand | cf::Op8);
        map.mark(2, cf::Opcode);
        map.mark(3, cf::Operand | cf::Op8 | cf::Op16);
        map.mark(10, cf::Data);
        map.mark(11, cf::Data | cf::Dma);
        map.mark(20, cf::Dma);
        auto c = map.coverage();
        CHECK(c.size == 64);
        CHECK(c.opcode == 2);
        CHECK(c.operand == 2);
        CHECK(c.code == 4);
        CHECK(c.both_widths == 1);
        CHECK(c.data == 2);
        CHECK(c.dma == 2);
        CHECK(c.seen == 7);
    }

    TEST_CASE("merging is a union, and only maps of the same size merge")
    {
        CodeMap a(8), b(8), c(9);
        a.mark(1, cf::Opcode);
        b.mark(1, cf::Data);
        b.mark(5, cf::Dma);
        CHECK(a.merge(b));
        CHECK(a.at(1) == (cf::Opcode | cf::Data));
        CHECK(a.at(5) == cf::Dma);
        CHECK(!a.merge(c));
        CHECK(a.at(1) == (cf::Opcode | cf::Data));
    }

    TEST_CASE("the fingerprint changes whenever a flag is added, and only then")
    {
        CodeMap map(16);
        auto f0 = map.fingerprint();
        map.mark(3, cf::Opcode);
        auto f1 = map.fingerprint();
        CHECK(f1 != f0);
        map.mark(3, cf::Opcode);
        CHECK(map.fingerprint() == f1);
        map.mark(3, cf::Op8);
        CHECK(map.fingerprint() != f1);
    }

    TEST_CASE("the file is run-length encoded and round-trips")
    {
        CodeMap map(6 * 1024 * 1024); // FF6's size, mostly unexplored
        for (uint32_t i = 0x1000; i < 0x1400; i++)
            map.mark(i, (i & 1) ? cf::Operand : cf::Opcode);
        map.mark(0x500000, cf::Dma);
        auto file = encode_code_map(map);
        CHECK(file.size() < 4096);
        std::string error;
        auto back = decode_code_map(file, error);
        REQUIRE(back);
        CHECK(error.empty());
        CHECK(back->flags() == map.flags());
    }

    TEST_CASE("broken files are refused, never trusted")
    {
        CodeMap map(100);
        map.mark(7, cf::Data);
        auto good = encode_code_map(map);
        std::string error;
        CHECK(!decode_code_map("", error));
        CHECK(!error.empty());
        CHECK(!decode_code_map("NOTAMAP!xxxxxxxx", error));
        CHECK(!decode_code_map(good.substr(0, good.size() - 1), error)); // runs stop short of the size
        auto extra = good + std::string("\x10\x05", 2);                  // runs past the size
        CHECK(!decode_code_map(extra, error));
        auto zero = good;
        zero.back() = 0; // a run of length 0
        CHECK(!decode_code_map(zero, error));
        std::string huge = good.substr(0, 8) + std::string("\xFF\xFF\xFF\x7F", 4); // 2 GB
        CHECK(!decode_code_map(huge, error));
        std::string endless = good.substr(0, 12) + std::string("\x01\xFF\xFF\xFF\xFF\xFF\xFF", 7); // varint never ends
        CHECK(!decode_code_map(endless, error));
    }

    TEST_CASE("the instruction a byte belongs to, at the width it ran")
    {
        // $8000: LDA #$1234 (16-bit A), $8003: BNE -5, $8005: LDX #$12 (8-bit X), $8007: data
        std::vector<uint8_t> rom = { 0xA9, 0x34, 0x12, 0xD0, 0xFB, 0xA2, 0x12, 0x99 };
        CodeMap map(rom.size());
        map.mark(0, cf::Opcode | cf::Op16);
        map.mark(1, cf::Operand | cf::Op16);
        map.mark(2, cf::Operand | cf::Op16);
        map.mark(3, cf::Opcode);
        map.mark(4, cf::Operand);
        map.mark(5, cf::Opcode | cf::Op8);
        map.mark(6, cf::Operand | cf::Op8);
        map.mark(7, cf::Data);
        RomAddresses where;
        where.chunks = { 0x808000 };

        auto i = instruction_at(rom, map, 2, where);
        REQUIRE(i);
        CHECK(i->start == 0);
        CHECK(i->length == 3);
        CHECK(i->address == 0x808000);
        CHECK(i->text == "LDA #$1234");
        CHECK(i->cls == OpClass::Load);
        CHECK(!i->both_widths);

        i = instruction_at(rom, map, 3, where);
        REQUIRE(i);
        CHECK(i->text == "BNE $8000");
        CHECK(i->address == 0x808003);

        i = instruction_at(rom, map, 6, where);
        REQUIRE(i);
        CHECK(i->start == 5);
        CHECK(i->text == "LDX #$12");

        CHECK(!instruction_at(rom, map, 7, where)); // data, not code
        CHECK(!instruction_at(rom, map, 99, where));
    }

    TEST_CASE("code run at both widths reads as its 8-bit form, flagged")
    {
        std::vector<uint8_t> rom = { 0xA9, 0x34, 0x12 };
        CodeMap map(rom.size());
        map.mark(0, cf::Opcode | cf::Op8 | cf::Op16);
        map.mark(1, cf::Operand | cf::Op8 | cf::Op16);
        map.mark(2, cf::Operand | cf::Op16);
        auto i = instruction_at(rom, map, 2, RomAddresses{}); // only the 16-bit form covers it
        REQUIRE(i);
        CHECK(i->length == 3);
        CHECK(i->both_widths);
        CHECK(i->address == std::nullopt);
        i = instruction_at(rom, map, 1, RomAddresses{});
        REQUIRE(i);
        CHECK(i->length == 2);
        CHECK(i->text == "LDA #$34");
    }

    TEST_CASE("ROM offsets to CPU addresses, a 4 KB chunk at a time")
    {
        RomAddresses where;
        where.chunks = { 0xC00000, RomAddresses::kNone, 0xC02000 };
        CHECK(where.cpu_address(0x0123) == 0xC00123);
        CHECK(where.cpu_address(0x1123) == std::nullopt);
        CHECK(where.cpu_address(0x2FFF) == 0xC02FFF);
        CHECK(where.cpu_address(0x3000) == std::nullopt);
    }

    TEST_CASE("the keeper saves when this person's map grew, and combines everyone's")
    {
        CodeMaps loaded;
        loaded.mine = CodeMap(32);
        loaded.others = CodeMap(32);
        loaded.others.mark(9, cf::Dma);
        CodeMapKeeper keeper;
        keeper.reset(32, std::move(loaded));
        CHECK(!keeper.unsaved());
        for (uint64_t f = 0; f < CodeMapKeeper::kSaveEveryFrames; f++)
            keeper.tick();
        CHECK(!keeper.save_due()); // nothing new
        keeper.mine().mark(4, cf::Opcode);
        CHECK(keeper.unsaved());
        CHECK(!keeper.save_due()); // the minute starts over after a check
        for (uint64_t f = 0; f < CodeMapKeeper::kSaveEveryFrames; f++)
            keeper.tick();
        CHECK(keeper.save_due());
        keeper.saved();
        CHECK(!keeper.unsaved());
        auto all = keeper.combined();
        CHECK(all.at(4) == cf::Opcode);
        CHECK(all.at(9) == cf::Dma);
        CHECK(keeper.coverage().seen == 2);
    }

    TEST_CASE("the keeper starts maps of the wrong size over")
    {
        CodeMaps loaded;
        loaded.mine = CodeMap(10);
        CodeMapKeeper keeper;
        keeper.reset(32, std::move(loaded));
        CHECK(keeper.mine().size() == 32);
        CHECK(keeper.combined().size() == 32);
    }

    TEST_CASE("the library keeps one file per person and loads them all")
    {
        TempDir tmp;
        Library lib(tmp.path);
        GameId game{ "TEST", "abcdef0123456789" };
        CodeMap mine(16), erik(16), wrong(8);
        mine.mark(1, cf::Opcode);
        erik.mark(2, cf::Data);
        CHECK(lib.save_code_map(game, "mikael", mine).empty());
        CHECK(lib.save_code_map(game, "erik", erik).empty());
        CHECK(lib.code_map_path(game, "mikael").filename() == "codemap.mikael.bin");
        auto other = lib.code_map_path(game, "old");
        {
            std::ofstream out(other, std::ios::binary);
            auto bytes = encode_code_map(wrong);
            out.write(bytes.data(), std::streamsize(bytes.size()));
        }
        {
            std::ofstream out(lib.code_map_path(game, "broken"), std::ios::binary);
            out << "garbage";
        }
        auto maps = lib.load_code_maps(game, "mikael", 16);
        CHECK(maps.mine.at(1) == cf::Opcode);
        CHECK(maps.mine.at(2) == 0);
        CHECK(maps.others.at(2) == cf::Data);
        CHECK(maps.others.at(1) == 0);
        CHECK(maps.errors.size() == 2); // the wrong size and the garbage
    }
}
