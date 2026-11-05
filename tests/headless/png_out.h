#pragma once
#include "corrupt/picture.h"

#include <string>
#include <vector>

// Pictures to PNG files, for the explorer's strips (#45). Headless tools only (libpng).
namespace headless
{

// Writes `p` as an RGB PNG. False on failure.
bool write_png(const std::string &path, const corrupt::Picture &p);

// Pictures side by side, 4 pixels apart, on white.
corrupt::Picture strip_of(const std::vector<corrupt::Picture> &ps);

} // namespace headless
