#include "journal.h"

#include <cstdio>

#include "nlohmann/json.hpp"

#include <fstream>

namespace corrupt
{

namespace
{

const char *event_name(JournalEntry::Kind k)
{
    switch (k)
    {
    case JournalEntry::Crash: return "crash";
    case JournalEntry::Recovered: return "recovered";
    case JournalEntry::GaveUp: return "gave_up";
    case JournalEntry::InstantRevert: return "instant_revert";
    case JournalEntry::FirstUse: return "first_use";
    }
    return "?";
}

// "2026-10-30_15-22-01_x.srm" -> "2026-10-30T15:22:01Z", reusing the backup naming's UTC math.
std::string iso_time(BackupClock::time_point t)
{
    std::string name = backup_file_name(t, "x");
    std::string date = name.substr(0, 10);
    std::string time = name.substr(11, 8);
    for (auto &c : time)
        if (c == '-')
            c = ':';
    return date + "T" + time + "Z";
}

} // namespace

std::string journal_line(const JournalEntry &e, BackupClock::time_point now)
{
    nlohmann::ordered_json j;
    j["event"] = event_name(e.kind);
    j["time"] = iso_time(now);
    j["recipe"] = e.recipe;
    j["seed"] = e.seed;
    j["policy"] = e.policy;
    if (e.kind != JournalEntry::Recovered)
    {
        j["signal"] = e.signal;
        j["text"] = e.text;
        j["frame"] = e.frame;
        j["since_frame"] = e.since_frame;
        j["restored_frame"] = e.restored_frame;
        j["frames_lost"] = e.frames_lost;
        j["suspects"] = e.suspects;
        j["action"] = e.action;
        j["attempt"] = e.attempt;
        if (!e.rom_blamed.empty() || !e.rom_action.empty())
        {
            j["rom_action"] = e.rom_action;
            auto bytes = nlohmann::ordered_json::array();
            for (auto o : e.rom_blamed)
            {
                char hex[16];
                std::snprintf(hex, sizeof hex, "0x%X", o);
                bytes.push_back(hex);
            }
            j["rom_bytes"] = bytes;
        }
    }
    else
        j["frame"] = e.frame;
    return j.dump();
}

std::string append_journal(const std::filesystem::path &path, const std::string &line)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::app | std::ios::binary);
    if (!out)
        return "couldn't open " + path.string();
    out << line << '\n';
    return out ? std::string() : "couldn't write " + path.string();
}

} // namespace corrupt
