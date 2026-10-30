#include "doctest/doctest.h"
#include "corrupt/save_backup.h"

#include <algorithm>
#include <fstream>
#include <set>

using namespace corrupt;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace
{
const BackupClock::time_point kNow = BackupClock::time_point(1'793'000'000s); // late Oct 2026

struct TempDir
{
    fs::path path;
    TempDir()
    {
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("corrupt-backup-test-" + std::to_string(stamp));
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
};
} // namespace

TEST_SUITE("rate limit")
{
    TEST_CASE("first backup happens as soon as there is something")
    {
        CHECK(should_backup(kNow, std::nullopt, 123, std::nullopt));
    }

    TEST_CASE("unchanged save RAM is never backed up again")
    {
        CHECK_FALSE(should_backup(kNow, kNow - 1h, 123, 123));
    }

    TEST_CASE("changed save RAM waits for the interval")
    {
        CHECK_FALSE(should_backup(kNow, kNow - 30s, 456, 123));
        CHECK(should_backup(kNow, kNow - 60s, 456, 123));
    }
}

TEST_CASE("hashing notices a single changed byte")
{
    std::vector<uint8_t> a(0x2000, 0), b = a;
    b[0x1234] = 1;
    CHECK(sram_hash(a.data(), a.size()) != sram_hash(b.data(), b.size()));
    CHECK(sram_hash(a.data(), a.size()) == sram_hash(a.data(), a.size()));
}

TEST_SUITE("retention")
{
    std::vector<BackupInfo> every_hour_for(int days)
    {
        std::vector<BackupInfo> files;
        for (int h = 0; h < days * 24; h++)
            files.push_back({ fs::path("b" + std::to_string(h) + ".srm"), kNow - std::chrono::hours(h) });
        return files;
    }

    TEST_CASE("keeps the newest 50")
    {
        auto files = every_hour_for(2); // 48 files, all within 2 days
        CHECK(backups_to_delete(files, kNow).empty());

        files = every_hour_for(3); // 72 files: 50 newest + 1 per older day survives
        auto doomed = backups_to_delete(files, kNow);
        CHECK(doomed.size() < 72 - 50);
        for (auto &d : doomed)
            CHECK(d.time < kNow - 49h);
    }

    TEST_CASE("keeps one per day for 30 days, and nothing older")
    {
        auto files = every_hour_for(40);
        auto doomed = backups_to_delete(files, kNow);
        std::set<std::string> gone;
        for (auto &d : doomed)
            gone.insert(d.path.string());

        std::set<long> kept_days;
        int kept_older_than_30 = 0;
        for (auto &f : files)
        {
            if (gone.count(f.path.string()))
                continue;
            auto age = kNow - f.time;
            if (age > 31 * 24h)
                kept_older_than_30++;
            kept_days.insert(long(std::chrono::duration_cast<std::chrono::hours>(f.time.time_since_epoch()).count() / 24));
        }
        CHECK(kept_older_than_30 == 0);
        CHECK(kept_days.size() >= 30);
        CHECK(files.size() - doomed.size() <= 50 + 31);
    }

    TEST_CASE("the newest backup of a day is the one kept")
    {
        // Three backups on one old day: only the latest survives.
        auto day = kNow - 10 * 24h;
        std::vector<BackupInfo> files;
        for (int i = 0; i < 50; i++)
            files.push_back({ fs::path("new" + std::to_string(i)), kNow - std::chrono::minutes(i) });
        auto midnight = BackupClock::time_point(std::chrono::duration_cast<std::chrono::hours>(day.time_since_epoch()) / 24 * 24);
        files.push_back({ fs::path("early"), midnight + 1h });
        files.push_back({ fs::path("mid"), midnight + 12h });
        files.push_back({ fs::path("late"), midnight + 23h });
        auto doomed = backups_to_delete(files, kNow);
        std::set<std::string> gone;
        for (auto &d : doomed)
            gone.insert(d.path.string());
        CHECK(gone.count("early"));
        CHECK(gone.count("mid"));
        CHECK_FALSE(gone.count("late"));
    }
}

TEST_SUITE("files")
{
    TEST_CASE("names carry the UTC time and reason, and parse back")
    {
        auto name = backup_file_name(kNow, "load");
        CHECK(name.size() > 4);
        CHECK(name.substr(name.size() - 4) == ".srm");
        CHECK(name.find("_load") != std::string::npos);
        auto parsed = parse_backup_file_name(name);
        REQUIRE(parsed);
        CHECK(*parsed == kNow);
        CHECK_FALSE(parse_backup_file_name("random.srm"));
    }

    TEST_CASE("write, list newest first, prune")
    {
        TempDir tmp;
        std::vector<uint8_t> sram(0x2000, 0xAB);
        for (int i = 0; i < 3; i++)
        {
            auto r = write_backup(tmp.path, sram.data(), sram.size(), kNow - std::chrono::minutes(i), "auto");
            REQUIRE_MESSAGE(r.error.empty(), r.error);
        }
        std::ofstream(tmp.path / "not-a-backup.txt") << "x";

        auto list = list_backups(tmp.path);
        REQUIRE(list.size() == 3);
        CHECK(list[0].time == kNow);
        CHECK(list[2].time == kNow - 2min);
        CHECK(fs::file_size(list[0].path) == 0x2000);

        std::vector<uint8_t> back;
        REQUIRE(read_backup(list[1].path, back));
        CHECK(back == sram);

        CHECK(prune_backups(tmp.path, kNow) == 0);
    }

    TEST_CASE("a missing folder lists as empty")
    {
        CHECK(list_backups("/definitely/not/here").empty());
    }
}
