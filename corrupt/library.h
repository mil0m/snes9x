#pragma once
#include "recipe.h"
#include "recipe_json.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace corrupt
{

// "SUPER MARIOWORLD [a1b2c3d4]": the ROM title made filesystem-safe, plus the
// first 8 hex digits of its SHA-256 so different versions never share a folder.
std::string game_folder_name(const std::string &title, const std::string &sha256_hex);

// "Purple Link chaos" -> "purple-link-chaos"
std::string recipe_file_stem(const std::string &recipe_name);

struct RecipeEntry
{
    std::filesystem::path path;
    std::string name;             // the recipe's name, or the file name if it can't be read
    std::optional<Recipe> recipe; // empty when the file is broken
    std::vector<std::string> errors;
    bool sha_mismatch = false;    // made for another version of the ROM; addresses may differ
};

struct SaveResult
{
    std::filesystem::path path;
    std::string error; // empty on success
};

// The recipe library: one folder per game under any root, typically a clone of
// the shared library repo.
class Library
{
  public:
    explicit Library(std::filesystem::path root) : root(std::move(root)) {}

    std::filesystem::path game_dir(const GameId &game) const;

    std::vector<RecipeEntry> list_recipes(const GameId &game) const; // sorted by name
    SaveResult save_recipe(const GameId &game, const Recipe &recipe) const;

    Regions load_regions(const GameId &game, std::vector<std::string> &errors) const;
    std::string save_regions(const GameId &game, const Regions &regions) const; // error or empty

  private:
    std::string ensure_game_dir(const GameId &game) const;

    std::filesystem::path root;
};

// Writes to a temporary file, then renames it into place.
std::string write_file_atomic(const std::filesystem::path &path, const std::string &contents);

} // namespace corrupt
