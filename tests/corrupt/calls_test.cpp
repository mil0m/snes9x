#include "doctest/doctest.h"

#include "corrupt/calls.h"
#include "corrupt/library.h"

#include <chrono>
#include <filesystem>
#include <fstream>

#include <algorithm>

using namespace corrupt;

namespace
{

const Function *find(const std::vector<Function> &fs, uint32_t entry)
{
    auto it = std::find_if(fs.begin(), fs.end(), [&](const Function &f) { return f.entry == entry; });
    return it == fs.end() ? nullptr : &*it;
}

} // namespace

TEST_SUITE("call graph")
{
    TEST_CASE("a call log counts calls per site and frames per entry, not calls")
    {
        CallLog log;
        for (int i = 0; i < 500; i++) // a loading frame calls one function 500 times
            log.note(0x100, callop::Jsr, 0x9000);
        log.note(0x200, callop::Jsl, 0xA000);
        auto called = log.end_frame();
        CHECK(called == std::vector<uint32_t>{ 0x9000, 0xA000 });
        log.note(0x200, callop::Jsl, 0xA000);
        log.note(0x300, callop::Jsl, 0xA000); // another caller, same function
        called = log.end_frame();
        CHECK(called == std::vector<uint32_t>{ 0xA000 });
        CHECK(log.end_frame().empty());

        CHECK(log.size() == 3);
        CHECK(log.all().at({ 0x100, 0x9000 }).hits == 500);
        CHECK(log.all().at({ 0x100, 0x9000 }).opcode == callop::Jsr);
        CHECK(log.frames_run().at(0x9000) == 1);
        CHECK(log.frames_run().at(0xA000) == 2);
    }

    TEST_CASE("logs merge by adding up")
    {
        CallLog a, b;
        a.note(0x100, callop::Jsr, 0x9000);
        a.end_frame();
        b.note(0x100, callop::Jsr, 0x9000);
        b.note(0x100, callop::Jsr, 0x9000);
        b.note(0x400, callop::JsrIndexed, 0xB000);
        b.end_frame();
        b.note(0x100, callop::Jsr, 0x9000);
        b.end_frame();
        a.merge(b);
        CHECK(a.size() == 2);
        CHECK(a.all().at({ 0x100, 0x9000 }).hits == 4);
        CHECK(a.frames_run().at(0x9000) == 3);
        CHECK(a.frames_run().at(0xB000) == 1);
        CHECK(a.fingerprint() == 2);
    }

    TEST_CASE("the file round-trips, in offset order")
    {
        CallLog log;
        log.note(0x200, callop::Jsl, 0xA000);
        log.note(0x100, callop::Jsr, 0x9000);
        log.note(0x100, callop::Jsr, 0x9000);
        log.end_frame();
        std::string file = encode_call_log(log, 0x80000);
        CHECK(file.find("\"format\": \"snes9x-calls\"") != std::string::npos);
        CHECK(file.find("0x000100") < file.find("0x000200"));
        std::string error;
        auto back = decode_call_log(file, 0x80000, error);
        REQUIRE(back);
        CHECK(error.empty());
        CHECK(back->size() == 2);
        CHECK(back->all().at({ 0x100, 0x9000 }).hits == 2);
        CHECK(back->all().at({ 0x200, 0xA000 }).opcode == callop::Jsl);
        CHECK(back->frames_run().at(0x9000) == 1);
        CHECK(encode_call_log(*back, 0x80000) == file);
    }

    TEST_CASE("a bad file or another ROM size is refused with a reason")
    {
        std::string error;
        CHECK(!decode_call_log("not json", 0x80000, error));
        CHECK(error == "not a call file");
        CHECK(!decode_call_log(R"({"format": "snes9x-jumps", "rom_size": 524288, "calls": []})", 0x80000, error));
        CHECK(error == "not a call file");
        CHECK(!decode_call_log(R"({"format": "snes9x-calls", "rom_size": 1024, "calls": []})", 0x80000, error));
        CHECK(error == "recorded for another ROM size");
        // Entries past the ROM, or an opcode that isn't a call, are dropped, not fatal.
        auto log = decode_call_log(R"({"format": "snes9x-calls", "version": 1, "rom_size": 1024, "calls": [
            {"site": "0x000010", "op": "20", "target": "0x000100", "hits": 3},
            {"site": "0x000010", "op": "EA", "target": "0x000100", "hits": 3},
            {"site": "0x000010", "op": "22", "target": "0x009999", "hits": 3}],
            "functions": [{"entry": "0x000100", "frames": 2}, {"entry": "0x009999", "frames": 9}]})",
                                   1024, error);
        REQUIRE(log);
        CHECK(log->size() == 1);
        CHECK(log->frames_run().size() == 1);
    }

    TEST_CASE("functions: near, long or mixed, with calls, callers and frames")
    {
        CallKeeper keeper;
        auto &log = keeper.mine();
        log.note(0x100, callop::Jsr, 0x9000);
        log.note(0x110, callop::JsrIndexed, 0x9000);
        log.note(0x200, callop::Jsl, 0xA000);
        log.note(0x300, callop::Jsr, 0xB000);
        log.note(0x310, callop::Jsl, 0xB000);
        keeper.end_frame();
        auto fs = keeper.functions();
        REQUIRE(fs.size() == 3);
        auto *near = find(fs, 0x9000);
        REQUIRE(near);
        CHECK(near->type == CallType::Near);
        CHECK(near->calls == 2);
        CHECK(near->callers == 2);
        CHECK(near->frames == 1);
        CHECK(near->first_site == 0x100);
        CHECK(find(fs, 0xA000)->type == CallType::Long);
        CHECK(find(fs, 0xB000)->type == CallType::Mixed);
        CHECK(return_byte(CallType::Near) == kRts);
        CHECK(return_byte(CallType::Long) == kRtl);
        CHECK(return_byte(CallType::Mixed) == 0);
    }

    TEST_CASE("now: the frames of the last 5 seconds a function ran in, busiest now first")
    {
        CallKeeper keeper;
        auto &log = keeper.mine();
        // 0x9000 runs every frame for 10 s; 0xA000 every frame of the first 2 s only.
        for (uint32_t f = 0; f < 600; f++)
        {
            log.note(0x100, callop::Jsr, 0x9000);
            if (f < 120)
                log.note(0x200, callop::Jsr, 0xA000);
            keeper.end_frame();
        }
        auto fs = keeper.functions();
        REQUIRE(fs.size() == 2);
        CHECK(fs[0].entry == 0x9000);
        CHECK(fs[0].now == CallKeeper::kNowFrames);
        CHECK(fs[0].frames == 600);
        CHECK(fs[1].now == 0); // forgotten after 300 frames
        CHECK(fs[1].frames == 120);
        keeper.forget_now();
        CHECK(keeper.functions()[0].now == 0);
    }

    TEST_CASE("everyone's functions: frames add up, now is this session's")
    {
        CallLogs loaded;
        loaded.others.note(0x100, callop::Jsr, 0x9000);
        loaded.others.end_frame();
        loaded.others.note(0x500, callop::Jsl, 0xC000);
        loaded.others.end_frame();
        CallKeeper keeper;
        keeper.reset(std::move(loaded));
        keeper.mine().note(0x100, callop::Jsr, 0x9000);
        keeper.end_frame();
        auto fs = keeper.functions();
        REQUIRE(fs.size() == 2);
        CHECK(fs[0].entry == 0x9000); // runs now
        CHECK(fs[0].frames == 2);
        CHECK(fs[0].now == 1);
        CHECK(find(fs, 0xC000)->now == 0);
        CHECK(keeper.unsaved()); // mine has a call site it hasn't saved
        keeper.saved();
        CHECK(!keeper.unsaved());
    }

    TEST_CASE("saving is due once a minute, and only with new calls")
    {
        CallKeeper keeper;
        for (uint64_t i = 0; i < CallKeeper::kSaveEveryFrames; i++)
            keeper.end_frame();
        CHECK(!keeper.save_due()); // a minute, but nothing new
        keeper.mine().note(0x100, callop::Jsr, 0x9000);
        for (uint64_t i = 0; i + 1 < CallKeeper::kSaveEveryFrames; i++)
            keeper.end_frame();
        CHECK(!keeper.save_due());
        keeper.end_frame();
        CHECK(keeper.save_due());
        CHECK(!keeper.save_due()); // the minute starts over
    }

    TEST_CASE("busiest: the ones that ran in the most frames, no mixed, then in entry order")
    {
        std::vector<Function> fs = {
            { .entry = 0x300, .type = CallType::Near, .frames = 10 },
            { .entry = 0x100, .type = CallType::Long, .frames = 50 },
            { .entry = 0x200, .type = CallType::Mixed, .frames = 99 },
            { .entry = 0x400, .type = CallType::Near, .frames = 50 },
            { .entry = 0x500, .type = CallType::Near, .frames = 1 },
        };
        auto top = busiest(fs, 3);
        REQUIRE(top.size() == 3);
        CHECK(top[0].entry == 0x100);
        CHECK(top[1].entry == 0x300);
        CHECK(top[2].entry == 0x400);
        CHECK(busiest(fs, 0).size() == 4);
    }

    TEST_CASE("the library keeps one file per person and loads them all")
    {
        namespace fs = std::filesystem;
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto dir = fs::temp_directory_path() / ("corrupt-calls-test-" + std::to_string(stamp));
        fs::create_directories(dir);
        {
            Library lib(dir);
            GameId game{ "TEST", "abcdef0123456789" };
            CallLog mine, erik;
            mine.note(0x10, callop::Jsr, 0x100);
            mine.end_frame();
            erik.note(0x20, callop::Jsl, 0x200);
            erik.end_frame();
            CHECK(lib.save_call_log(game, "mikael", mine, 0x1000).empty());
            CHECK(lib.save_call_log(game, "erik", erik, 0x1000).empty());
            CHECK(lib.call_log_path(game, "mikael").filename() == "calls.mikael.json");
            CHECK(lib.save_call_log(game, "old", erik, 0x800).empty()); // another ROM size
            {
                std::ofstream out(lib.call_log_path(game, "broken"));
                out << "garbage";
            }
            auto logs = lib.load_call_logs(game, "mikael", 0x1000);
            CHECK(logs.mine.size() == 1);
            CHECK(logs.others.size() == 1);
            CHECK(logs.others.all().count(CallKey{ 0x20, 0x200 }));
            CHECK(logs.others.frames_run().at(0x200) == 1);
            CHECK(logs.errors.size() == 2);
        }
        fs::remove_all(dir);
    }
}

TEST_SUITE("call graph")
{
    TEST_CASE("a function is off when its first byte became a return, not when it always was one")
    {
        // Switched off: the clean ROM starts with LDA, now it returns at once.
        CHECK(function_off(CallType::Near, 0x60, 0xA9));
        CHECK(function_off(CallType::Long, 0x6B, 0x8B));
        // A stub that only returns (Pocky & Rocky's $82:E010, 1 byte) was never switched off.
        CHECK_FALSE(function_off(CallType::Near, 0x60, 0x60));
        CHECK_FALSE(function_off(CallType::Long, 0x6B, 0x6B));
        // The wrong return for how it's called, untouched code, and mixed calls aren't off.
        CHECK_FALSE(function_off(CallType::Near, 0x6B, 0xA9));
        CHECK_FALSE(function_off(CallType::Near, 0xA9, 0xA9));
        CHECK_FALSE(function_off(CallType::Mixed, 0x60, 0xA9));
    }
}
