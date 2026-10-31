#include "doctest/doctest.h"
#include "corrupt/journal.h"
#include "corrupt/library.h"

#include "nlohmann/json.hpp"

#include <chrono>
#include <fstream>

using namespace corrupt;
namespace fs = std::filesystem;

namespace
{
JournalEntry sample_crash()
{
    JournalEntry e;
    e.kind = JournalEntry::Crash;
    e.recipe = "Purple chaos";
    e.seed = 1337;
    e.policy = "reroll";
    e.signal = "cpu_stopped";
    e.text = "CPU stopped (STP at $80:1234)";
    e.frame = 500;
    e.since_frame = 500;
    e.restored_frame = 420;
    e.frames_lost = 80;
    e.suspects = { "noise", "poke" };
    e.action = "reroll";
    e.attempt = 2;
    return e;
}
} // namespace

TEST_CASE("a journal line is one line of JSON with everything in it")
{
    auto line = journal_line(sample_crash(), BackupClock::time_point(std::chrono::seconds(1793000000)));
    CHECK(line.find('\n') == std::string::npos);
    auto j = nlohmann::json::parse(line);
    CHECK(j["event"] == "crash");
    CHECK(j["time"] == "2026-10-26T07:33:20Z");
    CHECK(j["recipe"] == "Purple chaos");
    CHECK(j["seed"] == 1337);
    CHECK(j["signal"] == "cpu_stopped");
    CHECK(j["suspects"].size() == 2);
    CHECK(j["frames_lost"] == 80);
    CHECK(j["attempt"] == 2);
}

TEST_CASE("recovered and gave-up lines name their event")
{
    auto now = BackupClock::time_point(std::chrono::seconds(1793000000));
    JournalEntry r;
    r.kind = JournalEntry::Recovered;
    CHECK(nlohmann::json::parse(journal_line(r, now))["event"] == "recovered");
    JournalEntry g = sample_crash();
    g.kind = JournalEntry::GaveUp;
    CHECK(nlohmann::json::parse(journal_line(g, now))["event"] == "gave_up");
}

TEST_CASE("appending builds a JSON-lines file, one file per person")
{
    auto dir = fs::temp_directory_path() / ("corrupt-journal-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Library lib(dir);
    GameId game{ "ZELDA", "abcdef0123", "" };
    auto path = lib.journal_path(game, "Mikael Z!");
    CHECK(path.filename() == "journal.mikael-z.jsonl");

    auto now = BackupClock::time_point(std::chrono::seconds(1793000000));
    REQUIRE(append_journal(path, journal_line(sample_crash(), now)).empty());
    REQUIRE(append_journal(path, journal_line(sample_crash(), now)).empty());

    std::ifstream in(path);
    std::string line;
    int lines = 0;
    while (std::getline(in, line))
    {
        CHECK(nlohmann::json::parse(line)["event"] == "crash");
        lines++;
    }
    CHECK(lines == 2);
    fs::remove_all(dir);
}

TEST_CASE("ROM bytes blamed by use are in the line, as hex offsets")
{
    corrupt::JournalEntry e;
    e.kind = corrupt::JournalEntry::Crash;
    e.rom_blamed = { 0x1A3F, 0x20 };
    e.rom_action = "reverted 2 ROM bytes";
    auto line = corrupt::journal_line(e, corrupt::BackupClock::time_point{});
    CHECK(line.find("\"rom_action\":\"reverted 2 ROM bytes\"") != std::string::npos);
    CHECK(line.find("\"rom_bytes\":[\"0x1A3F\",\"0x20\"]") != std::string::npos);
    corrupt::JournalEntry plain;
    CHECK(corrupt::journal_line(plain, corrupt::BackupClock::time_point{}).find("rom_") == std::string::npos);
}
