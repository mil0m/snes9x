#include "doctest/doctest.h"
#include "corrupt/recipe_json.h"

#include <filesystem>
#include <fstream>
#include <sstream>

// The starter recipes shipped in the app must always parse.
TEST_CASE("every bundled example recipe is valid")
{
    int count = 0;
    for (auto &file : std::filesystem::directory_iterator(CORRUPT_EXAMPLES_DIR))
    {
        if (file.path().extension() != ".json")
            continue;
        std::ifstream in(file.path());
        std::stringstream text;
        text << in.rdbuf();
        auto result = corrupt::parse_recipe(text.str(), {});
        CHECK_MESSAGE(result.recipe.has_value(), file.path().filename().string() << ": "
                                                     << (result.errors.empty() ? "" : result.errors[0]));
        count++;
    }
    CHECK(count >= 6);
}
