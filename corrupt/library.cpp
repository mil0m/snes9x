#include "library.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace corrupt
{

namespace
{

std::optional<std::string> read_file(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

std::string game_folder_name(const std::string &title, const std::string &sha256_hex)
{
    std::string clean;
    for (unsigned char c : title)
    {
        bool hostile = c < 0x20 || c >= 0x7F || std::string_view("/\\:*?\"<>|").find(char(c)) != std::string_view::npos;
        char out = hostile ? '_' : char(c);
        if (out == ' ' && (clean.empty() || clean.back() == ' '))
            continue;
        clean += out;
    }
    while (!clean.empty() && clean.back() == ' ')
        clean.pop_back();
    if (clean.empty())
        clean = "Unknown game";

    std::string hash = sha256_hex.substr(0, 8);
    std::transform(hash.begin(), hash.end(), hash.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return clean + " [" + hash + "]";
}

std::string recipe_file_stem(const std::string &recipe_name)
{
    std::string stem;
    for (unsigned char c : recipe_name)
    {
        if (std::isalnum(c) && c < 0x80)
            stem += char(std::tolower(c));
        else if (!stem.empty() && stem.back() != '-')
            stem += '-';
    }
    while (!stem.empty() && stem.back() == '-')
        stem.pop_back();
    return stem.empty() ? "recipe" : stem;
}

std::string write_file_atomic(const fs::path &path, const std::string &contents)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec)
        return "couldn't create " + path.parent_path().string() + ": " + ec.message();

    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return "couldn't write " + tmp.string();
        out << contents;
        if (!out.flush())
            return "couldn't write " + tmp.string();
    }
    fs::rename(tmp, path, ec);
    if (ec)
    {
        fs::remove(tmp);
        return "couldn't save " + path.string() + ": " + ec.message();
    }
    return {};
}

fs::path Library::game_dir(const GameId &game) const
{
    return root / game_folder_name(game.title, game.sha256);
}

std::string Library::ensure_game_dir(const GameId &game) const
{
    auto ignore = game_dir(game) / ".gitignore";
    if (fs::exists(ignore))
        return {};
    return write_file_atomic(ignore, "# Save backups are personal and stay out of the shared library.\nsave-backups/\n");
}

fs::path Library::backup_dir(const GameId &game) const
{
    ensure_game_dir(game);
    auto dir = game_dir(game) / "save-backups";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

std::vector<RecipeEntry> Library::list_recipes(const GameId &game) const
{
    std::vector<RecipeEntry> entries;
    auto dir = game_dir(game) / "recipes";
    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        return entries;

    std::vector<std::string> region_errors;
    auto regions = load_regions(game, region_errors);

    for (auto &file : fs::directory_iterator(dir, ec))
    {
        if (!file.is_regular_file() || file.path().extension() != ".json")
            continue;

        RecipeEntry entry;
        entry.path = file.path();
        entry.name = file.path().stem().string();

        auto text = read_file(file.path());
        if (!text)
            entry.errors.push_back("couldn't read the file");
        else
        {
            auto parsed = parse_recipe(*text, regions);
            entry.errors = parsed.errors;
            if (parsed.recipe)
            {
                if (!parsed.recipe->name.empty())
                    entry.name = parsed.recipe->name;
                entry.sha_mismatch = !parsed.recipe->game.sha256.empty() && !game.sha256.empty() &&
                                     parsed.recipe->game.sha256 != game.sha256;
                entry.recipe = std::move(parsed.recipe);
            }
        }
        entries.push_back(std::move(entry));
    }

    std::sort(entries.begin(), entries.end(), [](const RecipeEntry &a, const RecipeEntry &b) { return a.name < b.name; });
    return entries;
}

SaveResult Library::save_recipe(const GameId &game, const Recipe &recipe) const
{
    SaveResult result;
    if (auto err = ensure_game_dir(game); !err.empty())
    {
        result.error = err;
        return result;
    }
    result.path = game_dir(game) / "recipes" / (recipe_file_stem(recipe.name) + ".json");
    result.error = write_file_atomic(result.path, write_recipe(recipe));
    return result;
}

Regions Library::load_regions(const GameId &game, std::vector<std::string> &errors) const
{
    auto text = read_file(game_dir(game) / "regions.json");
    if (!text)
        return {};
    auto parsed = parse_regions(*text);
    errors.insert(errors.end(), parsed.errors.begin(), parsed.errors.end());
    return parsed.regions;
}

std::string Library::save_regions(const GameId &game, const Regions &regions) const
{
    if (auto err = ensure_game_dir(game); !err.empty())
        return err;
    return write_file_atomic(game_dir(game) / "regions.json", write_regions(regions));
}

} // namespace corrupt
