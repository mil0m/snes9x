#pragma once
// Diagnostics for a playback that drifts: corrupt-smoke --keep and snes9x-explore's
// SNES9X_EXPLORE_DIAG use them to find the first frame a moment drifts and which of the core's
// state a save doesn't hold (how #47 found VBlankStarted).
#include "corrupt/memory.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace headless
{
// Every memory in one number.
uint64_t state_hash(const corrupt::MemoryAccess &memory);
// The core's globals as bytes.
std::map<std::string, std::vector<uint8_t>> core_globals();
void print_globals_diff(const std::map<std::string, std::vector<uint8_t>> &a,
                        const std::map<std::string, std::vector<uint8_t>> &b);
// Which blocks of two saved states differ ("CPU", "SND", ...), and where in them.
void print_state_diff(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b);
// Where the core is in its frame, and what drawing left behind ("V 225, EndY 223, ...").
std::string core_where();
} // namespace headless
