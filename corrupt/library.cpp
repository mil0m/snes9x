#include "library.h"
#include "rule_text.h"

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

fs::path Library::journal_path(const GameId &game, const std::string &user) const
{
    return game_dir(game) / ("journal." + recipe_file_stem(user) + ".jsonl");
}

fs::path Library::crash_map_path(const GameId &game, const std::string &user) const
{
    return game_dir(game) / ("crashmap." + recipe_file_stem(user) + ".json");
}

CrashMaps Library::load_crash_maps(const GameId &game, const std::string &user) const
{
    CrashMaps out;
    std::error_code ec;
    auto mine = crash_map_path(game, user).filename().string();
    for (auto &entry : fs::directory_iterator(game_dir(game), ec))
    {
        auto name = entry.path().filename().string();
        if (name.rfind("crashmap.", 0) != 0 || entry.path().extension() != ".json")
            continue;
        std::ifstream in(entry.path());
        std::stringstream text;
        text << in.rdbuf();
        auto parsed = parse_crash_map(text.str());
        if (!parsed.errors.empty())
        {
            out.errors.push_back(name + ": " + parsed.errors.front());
            continue;
        }
        (name == mine ? out.mine : out.others).merge(parsed.map);
    }
    return out;
}

std::string Library::save_crash_map(const GameId &game, const std::string &user, const CrashMap &map) const
{
    if (auto err = ensure_game_dir(game); !err.empty())
        return err;
    return write_file_atomic(crash_map_path(game, user), crash_map_json(map));
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

RegionsResult Library::read_regions(const GameId &game) const
{
    auto text = read_file(game_dir(game) / "regions.json");
    return text ? parse_regions(*text) : RegionsResult{};
}

std::string Library::write_regions_file(const GameId &game, const RegionsResult &regions) const
{
    if (auto err = ensure_game_dir(game); !err.empty())
        return err;
    return write_file_atomic(game_dir(game) / "regions.json", write_regions(regions.regions, regions.notes));
}

std::string Library::set_region(const GameId &game, const std::string &name, const AddressRange &range) const
{
    if (auto err = validate_region_name(name))
        return *err;
    SizedMemory nominal;
    if (auto err = validate_range(range, nominal))
        return *err;
    auto regions = read_regions(game);
    regions.regions[name] = range;
    return write_regions_file(game, regions);
}

std::vector<std::string> Library::recipes_using_region(const GameId &game, const std::string &name) const
{
    std::vector<std::string> out;
    for (auto &entry : list_recipes(game))
        if (entry.recipe)
            for (auto &rule : entry.recipe->rules)
                if (rule.region == name)
                {
                    out.push_back(entry.name);
                    break;
                }
    return out;
}

// Renames region `from` to `to` in regions.json and in every recipe that uses it;
// an empty `to` deletes it, leaving those rules with its range and no name.
RegionChange Library::rewrite_region(const GameId &game, const std::string &from, const std::string &to) const
{
    RegionChange change;
    auto regions = read_regions(game);
    auto it = regions.regions.find(from);
    if (it == regions.regions.end())
    {
        change.error = "there's no region called '" + from + "'";
        return change;
    }

    // Read the recipes while the old name still resolves.
    std::vector<std::pair<std::filesystem::path, Recipe>> rewrites;
    for (auto &entry : list_recipes(game))
    {
        if (!entry.recipe)
            continue;
        Recipe recipe = *entry.recipe;
        bool uses = false;
        for (auto &rule : recipe.rules)
            if (rule.region == from)
            {
                rule.region = to;
                uses = true;
            }
        if (uses)
            rewrites.emplace_back(entry.path, std::move(recipe));
    }

    AddressRange range = it->second;
    regions.regions.erase(it);
    auto notes = regions.notes.find(from);
    std::string note = notes == regions.notes.end() ? std::string() : notes->second;
    if (notes != regions.notes.end())
        regions.notes.erase(notes);
    if (!to.empty())
    {
        regions.regions[to] = range;
        if (!note.empty())
            regions.notes[to] = note;
    }
    if (auto err = write_regions_file(game, regions); !err.empty())
    {
        change.error = err;
        return change;
    }
    for (auto &[path, recipe] : rewrites)
    {
        if (auto err = write_file_atomic(path, write_recipe(recipe)); !err.empty())
        {
            change.error = err;
            return change;
        }
        change.recipes.push_back(path);
    }
    return change;
}

RegionChange Library::rename_region(const GameId &game, const std::string &from, const std::string &to) const
{
    RegionChange change;
    if (auto err = validate_region_name(to))
        change.error = *err;
    else if (read_regions(game).regions.count(to))
        change.error = "there's already a region called '" + to + "'";
    if (!change.error.empty())
        return change;
    return rewrite_region(game, from, to);
}

RegionChange Library::delete_region(const GameId &game, const std::string &name) const
{
    return rewrite_region(game, name, {});
}

fs::path anchor_path(const fs::path &recipe_path)
{
    auto path = recipe_path;
    path.replace_extension(".anchor.frz");
    return path;
}

std::string save_anchor(const fs::path &path, const std::vector<uint8_t> &state)
{
    return write_file_atomic(path, std::string(state.begin(), state.end()));
}

std::optional<std::vector<uint8_t>> load_anchor(const fs::path &path, std::string &error)
{
    auto text = read_file(path);
    if (!text)
    {
        error = "couldn't read " + path.filename().string();
        return std::nullopt;
    }
    return std::vector<uint8_t>(text->begin(), text->end());
}

} // namespace corrupt
