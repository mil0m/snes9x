#include "library.h"

#include <map>
#include "rule_text.h"

#include <algorithm>
#include <functional>
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

bool is_sync_leftover(const std::string &file_name)
{
    return file_name.find(".sync-conflict-") != std::string::npos || file_name.rfind(".syncthing.", 0) == 0 ||
           file_name.rfind("~syncthing~", 0) == 0 || file_name == ".stfolder" || file_name == ".stignore" ||
           file_name == ".stversions";
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
        if (name.rfind("crashmap.", 0) != 0 || entry.path().extension() != ".json" || is_sync_leftover(name))
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

fs::path Library::code_map_path(const GameId &game, const std::string &user) const
{
    return game_dir(game) / ("codemap." + recipe_file_stem(user) + ".bin");
}

CodeMaps Library::load_code_maps(const GameId &game, const std::string &user, size_t rom_size) const
{
    CodeMaps out;
    out.mine = CodeMap(rom_size);
    out.others = CodeMap(rom_size);
    std::error_code ec;
    auto mine = code_map_path(game, user).filename().string();
    for (auto &entry : fs::directory_iterator(game_dir(game), ec))
    {
        auto name = entry.path().filename().string();
        if (name.rfind("codemap.", 0) != 0 || entry.path().extension() != ".bin" || is_sync_leftover(name))
            continue;
        auto text = read_file(entry.path());
        std::string error;
        auto map = text ? decode_code_map(*text, error) : std::nullopt;
        if (!text)
            error = "couldn't read it";
        else if (map && map->size() != rom_size)
            error = "made for a ROM of " + std::to_string(map->size()) + " bytes";
        if (!error.empty())
        {
            out.errors.push_back(name + ": " + error);
            continue;
        }
        (name == mine ? out.mine : out.others).merge(*map);
    }
    return out;
}

std::string Library::save_code_map(const GameId &game, const std::string &user, const CodeMap &map) const
{
    if (auto err = ensure_game_dir(game); !err.empty())
        return err;
    return write_file_atomic(code_map_path(game, user), encode_code_map(map));
}

fs::path Library::jump_log_path(const GameId &game, const std::string &user) const
{
    return game_dir(game) / ("jumps." + recipe_file_stem(user) + ".json");
}

JumpLogs Library::load_jump_logs(const GameId &game, const std::string &user, size_t rom_size) const
{
    JumpLogs out;
    std::error_code ec;
    auto mine = jump_log_path(game, user).filename().string();
    for (auto &entry : fs::directory_iterator(game_dir(game), ec))
    {
        auto name = entry.path().filename().string();
        if (name.rfind("jumps.", 0) != 0 || entry.path().extension() != ".json" || is_sync_leftover(name))
            continue;
        auto text = read_file(entry.path());
        std::string error;
        auto log = text ? decode_jump_log(*text, rom_size, error) : std::nullopt;
        if (!text)
            error = "couldn't read it";
        if (!log)
        {
            out.errors.push_back(name + ": " + error);
            continue;
        }
        (name == mine ? out.mine : out.others).merge(*log);
    }
    return out;
}

std::string Library::save_jump_log(const GameId &game, const std::string &user, const JumpLog &log, size_t rom_size) const
{
    if (auto err = ensure_game_dir(game); !err.empty())
        return err;
    return write_file_atomic(jump_log_path(game, user), encode_jump_log(log, rom_size));
}

fs::path Library::call_log_path(const GameId &game, const std::string &user) const
{
    return game_dir(game) / ("calls." + recipe_file_stem(user) + ".json");
}

CallLogs Library::load_call_logs(const GameId &game, const std::string &user, size_t rom_size) const
{
    CallLogs out;
    std::error_code ec;
    auto mine = call_log_path(game, user).filename().string();
    for (auto &entry : fs::directory_iterator(game_dir(game), ec))
    {
        auto name = entry.path().filename().string();
        if (name.rfind("calls.", 0) != 0 || entry.path().extension() != ".json" || is_sync_leftover(name))
            continue;
        auto text = read_file(entry.path());
        std::string error;
        auto log = text ? decode_call_log(*text, rom_size, error) : std::nullopt;
        if (!text)
            error = "couldn't read it";
        if (!log)
        {
            out.errors.push_back(name + ": " + error);
            continue;
        }
        (name == mine ? out.mine : out.others).merge(*log);
    }
    return out;
}

std::string Library::save_call_log(const GameId &game, const std::string &user, const CallLog &log, size_t rom_size) const
{
    if (auto err = ensure_game_dir(game); !err.empty())
        return err;
    return write_file_atomic(call_log_path(game, user), encode_call_log(log, rom_size));
}

fs::path Library::names_path(const GameId &game, const std::string &user) const
{
    return game_dir(game) / ("names." + recipe_file_stem(user) + ".json");
}

FunctionNames Library::load_names(const GameId &game, const std::string &user) const
{
    FunctionNames out;
    std::error_code ec;
    auto mine = names_path(game, user).filename().string();
    std::vector<fs::path> files;
    for (auto &entry : fs::directory_iterator(game_dir(game), ec))
    {
        auto name = entry.path().filename().string();
        if (name.rfind("names.", 0) == 0 && entry.path().extension() == ".json" && !is_sync_leftover(name))
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end()); // the same order on every machine
    for (auto &path : files)
    {
        auto name = path.filename().string();
        auto text = read_file(path);
        std::string error;
        auto names = text ? decode_names(*text, error) : std::nullopt;
        if (!text)
            error = "couldn't read it";
        if (!names)
        {
            out.errors.push_back(name + ": " + error);
            continue;
        }
        if (name == mine)
            out.mine = std::move(*names);
        else
        {
            std::string who = name.substr(6, name.size() - 6 - 5); // names.<who>.json
            for (auto &[entry, n] : *names)
                out.others[entry].push_back({ who, n });
        }
    }
    return out;
}

std::string Library::save_names(const GameId &game, const std::string &user, const NameList &names) const
{
    if (auto err = ensure_game_dir(game); !err.empty())
        return err;
    return write_file_atomic(names_path(game, user), encode_names(names));
}

namespace
{

// "name:size:time;" for each file `pick` accepts in `dir`, in name order.
std::string stamp_of(const fs::path &dir, const std::function<bool(const std::string &)> &pick)
{
    std::vector<std::string> parts;
    std::error_code ec;
    for (auto &entry : fs::directory_iterator(dir, ec))
    {
        auto name = entry.path().filename().string();
        if (!entry.is_regular_file() || is_sync_leftover(name) || !pick(name))
            continue;
        std::error_code e2;
        auto size = entry.file_size(e2);
        auto time = entry.last_write_time(e2).time_since_epoch().count();
        parts.push_back(name + ":" + std::to_string((unsigned long long)size) + ":" + std::to_string((long long)time) + ";");
    }
    std::sort(parts.begin(), parts.end());
    std::string out;
    for (auto &p : parts)
        out += p;
    return out;
}

} // namespace

std::vector<std::string> Library::conflict_copies(const GameId &game) const
{
    std::vector<std::string> out;
    std::error_code ec;
    for (auto &entry : fs::recursive_directory_iterator(game_dir(game), ec))
    {
        auto name = entry.path().filename().string();
        if (entry.is_regular_file() && name.find(".sync-conflict-") != std::string::npos)
            out.push_back(fs::relative(entry.path(), game_dir(game)).generic_string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string Library::others_stamp(const GameId &game, const std::string &user) const
{
    std::string me = "." + recipe_file_stem(user) + ".";
    return stamp_of(game_dir(game), [&](const std::string &name) {
        for (auto prefix : { "crashmap.", "codemap.", "jumps.", "calls.", "names." })
            if (name.rfind(prefix, 0) == 0)
                return name.find(me) != std::string(prefix).size() - 1;
        return false;
    });
}

std::string Library::recipes_stamp(const GameId &game) const
{
    return stamp_of(game_dir(game) / "recipes", [](const std::string &name) {
        return name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0;
    });
}

std::vector<Library::Player> Library::players(const GameId &game) const
{
    std::map<std::string, Player> by_name;
    std::error_code ec;
    for (auto &file : fs::directory_iterator(game_dir(game), ec))
    {
        if (!file.is_regular_file(ec))
            continue;
        auto name = file.path().filename().string();
        if (is_sync_leftover(name))
            continue;
        for (auto prefix : { "crashmap.", "codemap.", "jumps.", "calls.", "names.", "journal." })
        {
            std::string pre = prefix;
            auto last_dot = name.rfind('.');
            if (name.rfind(pre, 0) != 0 || last_dot == std::string::npos || last_dot <= pre.size())
                continue;
            auto who = name.substr(pre.size(), last_dot - pre.size());
            auto &p = by_name[who];
            p.who = who;
            p.files++;
            auto time = file.last_write_time(ec);
            if (!ec && time > p.newest)
                p.newest = time;
            break;
        }
    }
    std::vector<Player> out;
    for (auto &[name, p] : by_name)
        out.push_back(p);
    return out;
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
        if (!file.is_regular_file() || file.path().extension() != ".json" ||
            is_sync_leftover(file.path().filename().string()))
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
