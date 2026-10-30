#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace corrupt
{

// Automatic, continuous backups of a game's save RAM (.srm) while corrupting.
// Backups are local only (save-backups/ is git-ignored in the library).

using BackupClock = std::chrono::system_clock;

uint64_t sram_hash(const uint8_t *data, size_t size); // FNV-1a

// Regular backups: only when the save RAM changed, at most once per interval.
// (Load-time and close-time backups skip this and always run.)
bool should_backup(BackupClock::time_point now,
                   std::optional<BackupClock::time_point> last_backup,
                   uint64_t current_hash,
                   std::optional<uint64_t> last_hash,
                   std::chrono::seconds min_interval = std::chrono::seconds(60));

struct BackupInfo
{
    std::filesystem::path path;
    BackupClock::time_point time;
};

// Retention: the newest `keep_last`, plus the newest backup of each of the last
// `daily_days` (UTC) days. Returns the ones to delete.
std::vector<BackupInfo> backups_to_delete(std::vector<BackupInfo> backups,
                                          BackupClock::time_point now,
                                          size_t keep_last = 50,
                                          int daily_days = 30);

// "2026-10-30_15-22-01_load.srm" (UTC), so listing never depends on file mtimes.
std::string backup_file_name(BackupClock::time_point time, const std::string &reason);
std::optional<BackupClock::time_point> parse_backup_file_name(const std::string &name);

struct BackupWriteResult
{
    std::filesystem::path path;
    std::string error; // empty on success
};

BackupWriteResult write_backup(const std::filesystem::path &dir, const uint8_t *data, size_t size,
                               BackupClock::time_point time, const std::string &reason);
std::vector<BackupInfo> list_backups(const std::filesystem::path &dir); // newest first
bool read_backup(const std::filesystem::path &path, std::vector<uint8_t> &out);
size_t prune_backups(const std::filesystem::path &dir, BackupClock::time_point now); // returns how many were deleted

} // namespace corrupt
