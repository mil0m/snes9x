#pragma once
#include "recipe.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace corrupt
{

// Named address ranges for one game (its regions.json).
using Regions = std::map<std::string, AddressRange>;

struct ParseResult
{
    std::optional<Recipe> recipe; // set only when there are no errors
    std::vector<std::string> errors;
};

struct RegionsResult
{
    Regions regions; // the valid entries, even when some failed
    std::vector<std::string> errors;
};

// Parses and validates a recipe. Ranges are checked against nominal memory sizes
// (SRAM up to 128 KB), because no game needs to be loaded to read the library;
// the engine checks them again against the loaded game.
ParseResult parse_recipe(std::string_view json_text, const Regions &regions);

// Canonical form: a fixed key order, 2-space indent, addresses as hex strings,
// defaults left out. parse -> write is idempotent, so diffs stay clean in git.
std::string write_recipe(const Recipe &recipe);

// "0x1F", "$1F" or "31", optionally negative, surrounding whitespace ignored.
// The same rules recipes use for numbers written as strings.
std::optional<int64_t> parse_number(std::string_view text);

RegionsResult parse_regions(std::string_view json_text);
std::string write_regions(const Regions &regions);

} // namespace corrupt
