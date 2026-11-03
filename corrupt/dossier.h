#pragma once
#include "calls.h"
#include "code_map.h"
#include "function_names.h"
#include "jump_tables.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The game dossier (roadmap #42, first try): everything the tracer has learned about a
// game, as one Markdown file a person can read and a model can annotate. The app's own
// work, no model needed. Plan: docs/corruption/2026-11-03-game-mapping-plan.md.
namespace corrupt
{

struct DossierInput
{
    std::string title;                 // the game, for the heading
    std::span<const uint8_t> rom;      // the clean ROM
    std::span<const uint8_t> flags;    // the code map, everyone's
    RomAddresses where;                // ROM offset -> CPU address
    RomOffsets offsets;                // CPU address -> ROM offset
    std::vector<Function> functions;   // everyone's (CallKeeper::functions)
    CallLog calls;                     // everyone's, for who calls whom
    std::vector<JumpTable> tables;     // find_jump_tables on everyone's jump logs
    FunctionNames names;               // people's names (and a model's, marked by who)
    std::vector<std::string> sources;  // the files that went in, for the header
    size_t max_functions = 120;        // the busiest this many, plus every named one
    size_t max_instructions = 60;      // per function's listing; past it only calls, jumps and registers
    size_t max_ram = 150;              // rows of the RAM index
};

// One memory access an instruction's operand names, as far as code alone can tell.
enum class AccessKind : uint8_t
{
    Ram,      // work RAM ($7E:0000-$7F:FFFF and its low mirror)
    Hardware, // a register ($2100-$21FF, $4016-$4017, $4200-$43FF)
    Rom,      // ROM data
    Save,     // save RAM (banks $70-$7D)
};

struct Access
{
    AccessKind kind = AccessKind::Ram;
    uint32_t address = 0; // RAM: $7E:xxxx form (24-bit); hardware: $21xx; ROM: CPU address
    bool read = false;
    bool write = false;
    bool direct = false;  // a direct-page operand, so the address assumes D = 0
    bool indexed = false; // ,X ,Y or through a pointer: a base, not every byte touched
};

// What the instruction at `at` (its opcode byte) touches. `bank` is the code's bank, taken
// as the data bank for absolute operands. Jumps, calls and immediates touch nothing.
std::optional<Access> operand_access(std::span<const uint8_t> rom, uint32_t at, int length, uint8_t bank);

// "INIDISP" for $2100, "MDMAEN" for $420B, "DMA3 A-bus addr" for $4332...; empty if unknown.
std::string register_name(uint16_t address);

std::string make_dossier(const DossierInput &in);

} // namespace corrupt
