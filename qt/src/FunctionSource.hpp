#pragma once
#include "corrupt/calls.h"
#include "corrupt/function_change.h"
#include "corrupt/patches.h"

#include <cstdint>
#include <utility>

#include <optional>
#include <string>
#include <vector>

// One function the call graph knows (#26), as the Functions window shows it (#34).
struct FunctionRow
{
    corrupt::Function f;
    std::optional<uint32_t> address;    // the CPU address it's called at, for showing
    bool off = false;                   // its entry holds the return byte now
    std::optional<corrupt::Switch> kept; // the recipe's switch for it, if any
    std::string label;                  // everyone's names for it (#59), empty if nobody named it
    std::string my_name;                // yours
    size_t body_bytes = 0;              // its code (#58); 0 when the map doesn't know it yet
    bool open_ended = false;            // it leaves through a jump table: some of it may be elsewhere
    std::vector<std::pair<uint32_t, uint32_t>> runs; // its code, as ROM ranges
    bool guarded = false;               // other rules and paint leave it alone
    std::optional<corrupt::Protect> protect; // the recipe protects it
    std::vector<corrupt::Change> changes;    // the recipe's changes in it (#60)
};

// What the Functions window needs from the running game.
struct FunctionSource
{
    virtual std::vector<FunctionRow> functions() = 0; // busiest now first
    virtual bool running() = 0;                       // corruption is on: flips happen
    virtual std::string flip(uint32_t entry) = 0;     // off <-> on at the next frame; error or empty
    // Into the recipe as a switch (replacing one on the same entry); error or empty.
    // A timer (#61): off for `for_frames` of every `every` frames (0, 0: none).
    virtual std::string keep(uint32_t entry, int hotkey, bool start_off, const std::string &note, uint32_t every = 0,
                             uint32_t for_frames = 0) = 0;
    virtual void forget(uint32_t entry) = 0; // out of the recipe; the ROM stays as it is
    // Guard it from other rules and paint (deep: what it calls too), or stop; error or empty.
    virtual std::string protect(uint32_t entry, bool on, bool deep) = 0;
    // Your name for it, shared through the library (empty removes it); error or empty.
    virtual std::string setName(uint32_t entry, const std::string &name) = 0;

    // Change a function (#60). What in its code can change (from the clean ROM).
    virtual std::vector<corrupt::ChangeOption> changeOptions(uint32_t entry) = 0;
    // The bytes there now, and in the clean ROM.
    virtual std::vector<uint8_t> romNow(uint32_t at, size_t n) = 0;
    virtual std::vector<uint8_t> romClean(uint32_t at, size_t n) = 0;
    // A call sent to another function: the bytes, or nothing if it can't go there.
    virtual std::optional<std::vector<uint8_t>> redirectBytes(const corrupt::ChangeOption &o, uint32_t entry) = 0;
    // Its value in (on) or the clean bytes back (off), at the next frame while corruption is on.
    virtual std::string applyChange(const corrupt::Change &c, bool on) = 0;
    virtual std::string keepChange(const corrupt::Change &c) = 0; // into the recipe (replacing one at the same place)
    virtual void forgetChange(uint32_t at) = 0;                    // out of the recipe; the ROM stays as it is
    virtual ~FunctionSource() = default;
};
