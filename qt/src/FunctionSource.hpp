#pragma once
#include "corrupt/calls.h"
#include "corrupt/patches.h"

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
    virtual ~FunctionSource() = default;
};
