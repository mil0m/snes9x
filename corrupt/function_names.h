#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Shared names (#59): what players call a game's functions ("gravity"), one file per person
// in the game's library folder, merged with everyone's like the code map. Names come from
// people; a model's (the optional map-game skill, #42) are its own file, names.claude.json,
// so they always show as "name (claude)", beside people's and never instead of them. Plans:
// docs/corruption/2026-11-02-function-changes-plan.md, 2026-11-03-game-mapping-plan.md.
namespace corrupt
{

using NameList = std::map<uint32_t, std::string>; // entry (ROM offset) -> name

// names.<you>.json: {"format": "snes9x-names", "version": 1, "functions": [{"entry":
// "0x009000", "name": "gravity"}]}, in entry order, one a line.
std::string encode_names(const NameList &names);
// Entries that don't parse, or have no name, are dropped; the rest is kept.
std::optional<NameList> decode_names(std::string_view file, std::string &error);

struct OtherName
{
    std::string who; // the file's person: names.<who>.json
    std::string name;
};

// A game's names: this person's (the only ones they write) and everyone else's.
struct FunctionNames
{
    NameList mine;
    std::map<uint32_t, std::vector<OtherName>> others; // by entry, in file order
    std::vector<std::string> errors;                    // files that couldn't be read

    // How the window shows it: "gravity", "jump (erik)", or both: "gravity · jump (erik)".
    // Someone else's name that's the same as yours isn't repeated. Empty when nobody named it.
    std::string label(uint32_t entry) const;
    // Your name for it; empty removes it.
    void set_mine(uint32_t entry, const std::string &name);
};

} // namespace corrupt
