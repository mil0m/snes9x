#include "Starters.hpp"
#include "corrupt/recipe_json.h"

#include <QDir>
#include <QFile>
#include <algorithm>

const std::vector<Starter> &bundled_starters()
{
    static const std::vector<Starter> list = [] {
        std::vector<Starter> out;
        for (auto &file : QDir(":/corruption-examples").entryList({ "*.json" }, QDir::Files, QDir::Name))
        {
            QFile f(":/corruption-examples/" + file);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            if (auto parsed = corrupt::parse_recipe(f.readAll().toStdString(), {}); parsed.recipe)
                out.push_back({ ":/corruption-examples/" + file, *parsed.recipe });
        }
        // The gentlest first, so the obvious pick is a mild one.
        std::stable_partition(out.begin(), out.end(),
                              [](const Starter &s) { return s.resource.endsWith("gentle-wram-noise.json"); });
        return out;
    }();
    return list;
}
