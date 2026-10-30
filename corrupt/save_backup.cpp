#include "save_backup.h"
#include "library.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>

namespace fs = std::filesystem;
using namespace std::chrono;

namespace corrupt
{

namespace
{

// Howard Hinnant's civil-date algorithms: portable UTC conversion without
// timegm/_mkgmtime.
int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + int64_t(doe) - 719468;
}

void civil_from_days(int64_t z, int &y, unsigned &m, unsigned &d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = unsigned(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = int(int64_t(yoe) + era * 400 + (m <= 2));
}

int64_t utc_day(BackupClock::time_point t)
{
    auto s = duration_cast<seconds>(t.time_since_epoch()).count();
    return s >= 0 ? s / 86400 : (s - 86399) / 86400;
}

} // namespace

uint64_t sram_hash(const uint8_t *data, size_t size)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < size; i++)
    {
        h ^= data[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

bool should_backup(BackupClock::time_point now, std::optional<BackupClock::time_point> last_backup,
                   uint64_t current_hash, std::optional<uint64_t> last_hash, seconds min_interval)
{
    if (last_hash && *last_hash == current_hash)
        return false;
    if (last_backup && now - *last_backup < min_interval)
        return false;
    return true;
}

std::vector<BackupInfo> backups_to_delete(std::vector<BackupInfo> backups, BackupClock::time_point now,
                                          size_t keep_last, int daily_days)
{
    std::sort(backups.begin(), backups.end(), [](auto &a, auto &b) { return a.time > b.time; });

    std::set<int64_t> days_kept;
    int64_t today = utc_day(now);
    std::vector<BackupInfo> doomed;

    for (size_t i = 0; i < backups.size(); i++)
    {
        int64_t day = utc_day(backups[i].time);
        bool recent_day = today - day < daily_days;
        bool first_of_day = days_kept.insert(day).second; // newest-first, so the first seen is the newest
        if (i < keep_last || (recent_day && first_of_day))
            continue;
        doomed.push_back(backups[i]);
    }
    return doomed;
}

std::string backup_file_name(BackupClock::time_point time, const std::string &reason)
{
    auto secs = duration_cast<seconds>(time.time_since_epoch()).count();
    int64_t day = utc_day(time);
    int64_t rem = secs - day * 86400;
    int y;
    unsigned m, d;
    civil_from_days(day, y, m, d);

    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02u-%02u_%02d-%02d-%02d", y, m, d,
             int(rem / 3600), int(rem / 60 % 60), int(rem % 60));
    return std::string(buf) + "_" + recipe_file_stem(reason) + ".srm";
}

std::optional<BackupClock::time_point> parse_backup_file_name(const std::string &name)
{
    int y, mo, d, h, mi, s;
    if (sscanf(name.c_str(), "%4d-%2d-%2d_%2d-%2d-%2d", &y, &mo, &d, &h, &mi, &s) != 6)
        return std::nullopt;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60)
        return std::nullopt;
    int64_t secs = days_from_civil(y, unsigned(mo), unsigned(d)) * 86400 + h * 3600 + mi * 60 + s;
    return BackupClock::time_point(seconds(secs));
}

BackupWriteResult write_backup(const fs::path &dir, const uint8_t *data, size_t size,
                               BackupClock::time_point time, const std::string &reason)
{
    BackupWriteResult result;
    std::string name = backup_file_name(time, reason);
    result.path = dir / name;
    for (int n = 2; fs::exists(result.path); n++) // two backups in the same second
        result.path = dir / (name.substr(0, name.size() - 4) + "-" + std::to_string(n) + ".srm");
    result.error = write_file_atomic(result.path, std::string(reinterpret_cast<const char *>(data), size));
    return result;
}

std::vector<BackupInfo> list_backups(const fs::path &dir)
{
    std::vector<BackupInfo> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        return out;
    for (auto &f : fs::directory_iterator(dir, ec))
    {
        if (!f.is_regular_file() || f.path().extension() != ".srm")
            continue;
        if (auto t = parse_backup_file_name(f.path().filename().string()))
            out.push_back({ f.path(), *t });
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.time > b.time; });
    return out;
}

bool read_backup(const fs::path &path, std::vector<uint8_t> &out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), {});
    return true;
}

size_t prune_backups(const fs::path &dir, BackupClock::time_point now)
{
    size_t removed = 0;
    for (auto &b : backups_to_delete(list_backups(dir), now))
    {
        std::error_code ec;
        if (fs::remove(b.path, ec))
            removed++;
    }
    return removed;
}

} // namespace corrupt
