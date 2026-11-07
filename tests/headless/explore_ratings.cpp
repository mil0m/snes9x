// explore-ratings (#83): what people's 👍/👎 on the Explored page say about the explorer's
// measures. Reads every exploration under a library (or one game's folder) with everyone's
// ratings, and prints how well each measure picks the good finds, how often two people
// agree, and good finds by kind.
//
//   explore-ratings <library or game folder>
#include "corrupt/library.h"
#include "corrupt/ratings.h"

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;
using namespace corrupt;

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <library or game folder>\n", argv[0]);
        return 2;
    }
    fs::path root = argv[1];
    std::vector<fs::path> explored;
    std::error_code ec;
    if (fs::is_directory(root / "explored", ec))
        explored.push_back(root / "explored");
    else
        for (auto &game : fs::directory_iterator(root, ec))
            if (game.is_directory() && fs::is_directory(game.path() / "explored", ec))
                explored.push_back(game.path() / "explored");
    if (explored.empty())
    {
        fprintf(stderr, "no explorations under %s\n", root.string().c_str());
        return 1;
    }
    std::vector<RatedExploration> list;
    std::vector<std::string> errors;
    for (auto &dir : explored)
        for (auto &e : Library::explorations_in(dir, &errors))
            list.push_back({ std::move(e.exploration), std::move(e.ratings) });
    for (auto &e : errors)
        fprintf(stderr, "skipped %s\n", e.c_str());
    printf("%zu exploration(s) in %zu game folder(s)\n", list.size(), explored.size());
    fputs(ratings_report(list).c_str(), stdout);
    return 0;
}
