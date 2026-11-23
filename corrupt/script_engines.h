#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Script engines (roadmap #43): bytecode interpreters in the game's code. An interpreter reads
// a command byte from its script in ROM and jumps to that command's handler through a table
// indexed by it. At every indirect jump the tracer notes what picked the handler (X, or the
// ROM table entry a RAM pointer was copied from) and the ROM bytes read as data just before.
// A rule `index = stride x command + bias` that keeps fitting, over many commands read from
// many places, is a script engine; the step from one command to the next it runs is a
// command's length. Plan: docs/corruption/2026-11-23-script-engines-plan.md.
namespace corrupt
{

constexpr int kScriptReads = 16; // how many recent ROM reads the core keeps
constexpr int kMaxStride = 4;
constexpr int kMaxStep = 32; // a command longer than this isn't believed

// One dispatch as it ran: what picked the handler, where it went, and the ROM data reads just
// before it, newest first, with their values in the clean ROM. For JMP/JSR (a,X) the index is
// X. For a jump through a pointer in RAM (JMP (a), JML [a]) it is the ROM offset of the table
// entry the game copied the pointer from: the newest read holding the target's address
// (`entry` is its position among the reads); without one there's no index.
struct ScriptObservation
{
    uint32_t index = 0;
    bool has_index = false;
    bool through_pointer = false;
    int8_t entry = -1;
    uint32_t target = 0;
    uint8_t reads = 0;
    uint32_t offset[kScriptReads] = {};
    uint8_t value[kScriptReads] = {};
};

// What one candidate rule saw at one place (the ROM offset of a command it ran).
struct ScriptPlace
{
    uint8_t value = 0;
    uint32_t runs = 0;
    uint32_t wild = 0;                // the next command it ran was elsewhere (behind, or too far; not here again)
    std::map<uint8_t, uint32_t> steps; // the next command started this many bytes on (1-32)
};

struct ScriptCandidateStats
{
    uint32_t dispatches = 0;
    std::map<uint8_t, uint32_t> handlers;              // command -> its handler (24-bit), last seen
    std::map<uint8_t, uint32_t> runs;                  // command -> how often
    std::unordered_map<uint32_t, ScriptPlace> places;   // ROM offset -> what ran there
    uint32_t last = 0xFFFFFFFF;                         // the place it ran last (not saved)
};

using ScriptRuleKey = std::pair<int, uint32_t>; // stride, bias

struct ScriptSite
{
    uint32_t dispatches = 0;
    bool through_pointer = false;
    std::map<ScriptRuleKey, ScriptCandidateStats> candidates;
};

// Everything the tracer saw at indirect jumps, kept as candidate rules per site. A dispatch
// fits up to 64 rules (16 reads x 4 strides), nearly all coincidences, so a site may grow to
// kPrune candidates and is then cut to its kCandidates busiest: a real engine has piled up
// dispatches by then even when it shares a trampoline with other dispatchers. Lives on the
// emulation thread. Logs add up: merging two gives what one log fed both would hold (but for
// which coincidental candidates were kept).
class ScriptLog
{
  public:
    static constexpr size_t kCandidates = 128;
    static constexpr size_t kPrune = 1024;
    void note(uint32_t site, const ScriptObservation &o);
    void merge(const ScriptLog &other);
    const std::map<uint32_t, ScriptSite> &all() const { return sites; }
    size_t size() const { return sites.size(); }
    bool empty() const { return sites.empty(); }
    // Grows with new places, so a caller can tell when it's worth saving.
    uint64_t fingerprint() const { return places_seen; }
    void clear() { *this = {}; }

    // For the file: replaces a site's stats wholesale.
    void put(uint32_t site, ScriptSite s) { sites[site] = std::move(s); }

  private:
    void make_room(ScriptSite &s);
    std::map<uint32_t, ScriptSite> sites;
    uint64_t places_seen = 0;
};

struct ScriptCommand
{
    uint8_t value = 0;
    uint32_t handler = 0;
    uint32_t runs = 0;
    int length = 0;              // bytes, the command included; 0 when unknown
    uint32_t samples = 0;        // steps 1-32 seen after it
    uint32_t wild = 0;           // steps elsewhere
    bool branchy = false;        // it often goes elsewhere: a branch, a call, a jump
    bool usable() const { return length > 0 && !branchy; }
};

struct ScriptEnginePlace
{
    uint32_t offset = 0;
    uint8_t value = 0;
    uint32_t runs = 0;
    bool steady = false; // every step seen from here is its command's length (and there was one)
};

struct ScriptEngine
{
    uint32_t site = 0;
    bool through_pointer = false;
    int stride = 2;
    uint32_t bias = 0;            // mod 64 K for X; the table's base for a pointer's entry
    uint32_t dispatches = 0;      // the rule fits these
    uint32_t site_dispatches = 0; // everything through the site
    std::map<uint8_t, ScriptCommand> commands;
    std::vector<ScriptEnginePlace> places; // by offset
    uint32_t first = 0, last = 0;          // the places' span
    double sequential = 0;                 // of the steps after its commands, the share 1-32 bytes on

    // The places #44 may change: steady, of a usable command.
    std::vector<ScriptEnginePlace> usable_places() const;
    // Commands of the same length that are usable, for a swap.
    std::vector<uint8_t> same_length(int length) const;
};

struct ScriptRules
{
    double share = 0.9;           // an indexed jump's best rule must explain this much of the site
    uint32_t min_dispatches = 20;
    size_t min_commands = 3;
    size_t min_commands_through_ram = 5; // a shared trampoline makes small coincidences
    size_t min_places = 8;
    double min_places_per_command = 2; // a script uses its commands again; a walk over records doesn't
    uint32_t min_length_samples = 3;
    double min_sequential = 0.6; // a script is read forwards: the next command a few bytes on
};

// The engines in a log, busiest first.
std::vector<ScriptEngine> find_script_engines(const ScriptLog &log, const ScriptRules &rules = {});

// The viewer's Scripts overlay: one mark per ROM byte.
namespace scriptmark
{
constexpr uint8_t Argument = 1; // after a usable command, within its length
constexpr uint8_t Command = 2;  // a command byte seen run, whose length isn't trusted
constexpr uint8_t Usable = 3;   // a command byte a script change may touch
} // namespace scriptmark
std::vector<uint8_t> script_marks(const std::vector<ScriptEngine> &engines, size_t rom_size);

// What the viewer says about one ROM byte: the place whose command or argument it is.
struct ScriptAt
{
    const ScriptEngine *engine = nullptr;
    const ScriptEnginePlace *place = nullptr;
    bool argument = false; // the byte is one of the place's arguments, not its command
};
ScriptAt script_at(const std::vector<ScriptEngine> &engines, uint32_t offset);

// What the library holds for a game: this person's log and everyone else's, merged.
struct ScriptLogs
{
    ScriptLog mine;
    ScriptLog others;
    std::vector<std::string> errors; // files that couldn't be read or are for another ROM
};

// The session's script log, kept like the jump log: the tracer notes into mine, saved once a
// minute when it learned new places, everyone's combined for finding engines.
class ScriptLogKeeper
{
  public:
    static constexpr uint64_t kSaveEveryFrames = 3600;
    void reset(ScriptLogs loaded = {});
    void tick() { frames++; }
    ScriptLog &mine() { return own; }
    const ScriptLog &mine() const { return own; }
    ScriptLog combined() const;
    uint64_t fingerprint() const { return own.fingerprint() + others.fingerprint(); }
    bool save_due(); // once a minute, when mine has new places; starts the minute over
    bool unsaved() const { return own.fingerprint() != saved_print; }
    void saved() { saved_print = own.fingerprint(); }

  private:
    ScriptLog own, others;
    uint64_t saved_print = 0;
    uint64_t frames = 0;
};

// The file: JSON, {"format": "snes9x-scripts", "version": 1, "rom_size": N, "rom_crc": "8 hex",
// "sites": [{"site": "0x009A8E", "through_pointer": true, "dispatches": N, "rules": [{"stride":
// 2, "bias": "0x978F", "dispatches": N, "commands": [["35", "0xC09B12", runs]...], "places":
// [["0x0A1234", "35", runs, wild, [[step, count]...]]...]}]}]}. Only the busiest `per_site`
// candidates of a site with at least `keep` dispatches and `min_places` places are written: the
// rest are coincidences or echoes of an engine. A file for another ROM (size or CRC) is refused.
std::string encode_script_log(const ScriptLog &log, size_t rom_size, uint32_t rom_crc, uint32_t keep = 20,
                              size_t min_places = 4, size_t per_site = 32);
std::optional<ScriptLog> decode_script_log(std::string_view file, size_t rom_size, uint32_t rom_crc, std::string &error);

} // namespace corrupt
