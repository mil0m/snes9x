#pragma once
#include "corrupt/calls.h"
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
};

// What the Functions window needs from the running game.
struct FunctionSource
{
    virtual std::vector<FunctionRow> functions() = 0; // busiest now first
    virtual bool running() = 0;                       // corruption is on: flips happen
    virtual std::string flip(uint32_t entry) = 0;     // off <-> on at the next frame; error or empty
    // Into the recipe as a switch (replacing one on the same entry); error or empty.
    virtual std::string keep(uint32_t entry, int hotkey, bool start_off, const std::string &note) = 0;
    virtual void forget(uint32_t entry) = 0; // out of the recipe; the ROM stays as it is
    // Guard it from other rules and paint (deep: what it calls too), or stop; error or empty.
    virtual std::string protect(uint32_t entry, bool on, bool deep) = 0;
    // Your name for it, shared through the library (empty removes it); error or empty.
    virtual std::string setName(uint32_t entry, const std::string &name) = 0;
    virtual ~FunctionSource() = default;
};
