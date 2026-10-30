#pragma once
#include "crash_guard.h"
#include "save_backup.h"

#include <filesystem>
#include <string>

namespace corrupt
{

// The crash journal: one JSON object per line, appended, one file per person
// (journal.<user>.jsonl) so a shared library never has merge conflicts.
// It is the raw material for comparing crash policies across games.

std::string journal_line(const JournalEntry &entry, BackupClock::time_point now);
std::string append_journal(const std::filesystem::path &path, const std::string &line); // error or empty

} // namespace corrupt
