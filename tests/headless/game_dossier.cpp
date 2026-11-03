// game-dossier: what the tracer has learned about a game, as one Markdown file (#42).
//
//   build/tests/game-dossier <rom> <game folder in the library> [--functions N]
//                            [--instructions N] [--out file]
//
// Reads everyone's calls.*.json, jumps.*.json, codemap.*.bin and names.*.json in the
// folder, loads the ROM in the core (for its address map), and writes corrupt/dossier.h's
// Markdown to stdout or --out. No model involved. Plan: docs/corruption/2026-11-03-game-mapping-plan.md.
#include "headless_core.h"

#include "corrupt/calls.h"
#include "corrupt/code_map.h"
#include "corrupt/core_access.h"
#include "corrupt/dossier.h"
#include "corrupt/function_names.h"
#include "corrupt/jump_tables.h"

#include "snes9x.h"
#include "memmap.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace corrupt;
namespace fs = std::filesystem;

namespace
{

std::string read_file(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

// "calls.erbl.json" -> "erbl"; empty for files that aren't one of ours (sync leftovers).
std::string who(const std::string &name, const std::string &prefix, const std::string &ext)
{
    if (name.rfind(prefix, 0) != 0 || name.size() <= prefix.size() + ext.size() ||
        name.compare(name.size() - ext.size(), ext.size(), ext) != 0)
        return {};
    auto w = name.substr(prefix.size(), name.size() - prefix.size() - ext.size());
    return w.find('.') == std::string::npos && w.find(' ') == std::string::npos ? w : std::string();
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <rom> <game folder> [--functions N] [--instructions N] [--out file]\n", argv[0]);
        return 2;
    }
    DossierInput in;
    std::string out_path;
    for (int i = 3; i + 1 < argc; i += 2)
    {
        std::string flag = argv[i];
        if (flag == "--functions")
            in.max_functions = size_t(std::stoul(argv[i + 1]));
        else if (flag == "--instructions")
            in.max_instructions = size_t(std::stoul(argv[i + 1]));
        else if (flag == "--out")
            out_path = argv[i + 1];
    }
    auto t0 = std::chrono::steady_clock::now();
    if (!headless::init() || !headless::load_rom_file(argv[1]))
    {
        fprintf(stderr, "couldn't load %s\n", argv[1]);
        return 1;
    }
    size_t size = Memory.CalculatedSize;
    std::vector<uint8_t> rom(Memory.ROM, Memory.ROM + size);
    in.title = Memory.ROMName;
    in.where = SnesCoreAccess::rom_addresses();
    in.offsets = SnesCoreAccess::rom_offsets();

    CodeMap map(size);
    CallLog calls;
    JumpLog jumps;
    for (auto &entry : fs::directory_iterator(argv[2]))
    {
        auto name = entry.path().filename().string();
        std::string error, w;
        if (!(w = who(name, "codemap.", ".bin")).empty())
        {
            if (auto m = decode_code_map(read_file(entry.path()), error); m && map.merge(*m))
                in.sources.push_back(name);
            else
                fprintf(stderr, "skipped %s: %s\n", name.c_str(), error.empty() ? "another ROM size" : error.c_str());
        }
        else if (!(w = who(name, "calls.", ".json")).empty())
        {
            if (auto c = decode_call_log(read_file(entry.path()), size, error))
            {
                calls.merge(*c);
                in.sources.push_back(name);
            }
            else
                fprintf(stderr, "skipped %s: %s\n", name.c_str(), error.c_str());
        }
        else if (!(w = who(name, "jumps.", ".json")).empty())
        {
            if (auto j = decode_jump_log(read_file(entry.path()), size, error))
            {
                jumps.merge(*j);
                in.sources.push_back(name);
            }
            else
                fprintf(stderr, "skipped %s: %s\n", name.c_str(), error.c_str());
        }
        else if (!(w = who(name, "names.", ".json")).empty())
        {
            if (auto n = decode_names(read_file(entry.path()), error))
            {
                for (auto &[e, label] : *n)
                    in.names.others[e].push_back({ w, label });
                in.sources.push_back(name);
            }
            else
                fprintf(stderr, "skipped %s: %s\n", name.c_str(), error.c_str());
        }
    }
    std::sort(in.sources.begin(), in.sources.end());
    in.rom = rom;
    in.flags = map.flags();
    CallKeeper keeper;
    keeper.reset(CallLogs{ {}, calls, {} });
    in.functions = keeper.functions();
    in.calls = calls;
    in.tables = find_jump_tables(jumps, rom, map.flags(), in.offsets).tables;

    auto text = make_dossier(in);
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (out_path.empty())
        fwrite(text.data(), 1, text.size(), stdout);
    else
        std::ofstream(out_path, std::ios::binary) << text;
    fprintf(stderr, "dossier: %zu functions, %zu tables, %zu bytes, %.1f s\n", in.functions.size(), in.tables.size(),
            text.size(), seconds);
    return 0;
}
