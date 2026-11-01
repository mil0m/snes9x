#pragma once
#include "corrupt/recipe.h"

#include <QString>
#include <vector>

// The example recipes that come with the app (the corruption_examples resources), the
// gentlest first. Shared by the panel's recipe list and the remote page (#53).
struct Starter
{
    QString resource;
    corrupt::Recipe recipe;
};
const std::vector<Starter> &bundled_starters();
