#pragma once
#include "rule.h"

#include <string>
#include <vector>

namespace corrupt
{

struct GameId
{
    std::string title;
    std::string sha256; // lowercase hex of the ROM's SHA-256
    std::string crc32;  // 8 hex digits, informational
};

struct Recipe
{
    static constexpr int kFormat = 1;

    std::string name;
    std::string notes;
    GameId game;
    uint64_t seed = 0;
    std::string anchor_state; // phase 3: save state the recipe starts from
    std::vector<Rule> rules;
};

} // namespace corrupt
