#pragma once
#include "corrupt/area_ops.h"

#include <QImage>
#include <QString>

#include <optional>
#include <utility>
#include <vector>

// Memory as a picture for an image editor, the way ROMs used to be corrupted in Photoshop:
// one 8-bit grey pixel a byte, in rows of a chosen width. The picture carries where it came
// from (a PNG text chunk), so an edited copy finds its way back; the pixels that changed
// become bytes to set.
struct ImageArea
{
    corrupt::Area area; // what the picture covers, row by row
    uint32_t last = 0;  // the last byte that belongs to it: a range's last row can be short
};

QImage areaToImage(const std::vector<uint8_t> &bytes, const ImageArea &where);

// Where a picture came from, if it says (pictures this app wrote do; editors may drop it).
std::optional<ImageArea> imageArea(const QImage &image);

// The bytes the picture changes, as (offset, value). Colour pictures read as their
// brightness. nullopt, with `error` set, when the picture isn't the area's size.
std::optional<std::vector<std::pair<uint32_t, uint8_t>>>
imageToBytes(const QImage &image, const ImageArea &where, const std::vector<uint8_t> &bytes, QString *error);
