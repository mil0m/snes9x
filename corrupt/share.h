#pragma once
#include "recipe.h"

#include <optional>
#include <string>
#include <vector>

namespace corrupt
{

// A recipe to send to a friend: one .s9xrecipe file (JSON) with the recipe, its pins and
// the moment its run starts from, so it needs nothing from the sender's library. Named
// areas become plain ranges.
struct SharedRecipe
{
    Recipe recipe;
    std::optional<std::vector<uint8_t>> moment; // a save state
};

std::string write_shared(const Recipe &recipe, const std::vector<uint8_t> *moment);
std::optional<SharedRecipe> read_shared(const std::string &text, std::string &error);

} // namespace corrupt
