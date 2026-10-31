#include "AreaImage.hpp"

#include <QObject>
#include <QStringList>

using namespace corrupt;

namespace
{
constexpr const char *kKey = "snes9x-area"; // "<target> <start> <row> <width> <height> <last>", hex where it helps
}

QImage areaToImage(const std::vector<uint8_t> &bytes, const ImageArea &where)
{
    const Area &a = where.area;
    QImage img(int(a.width), int(a.height), QImage::Format_Grayscale8);
    for (uint32_t y = 0; y < a.height; y++)
    {
        uchar *line = img.scanLine(int(y));
        for (uint32_t x = 0; x < a.width; x++)
        {
            uint32_t o = a.at(x, y);
            line[x] = o <= where.last && o < bytes.size() ? bytes[o] : 0;
        }
    }
    img.setText(kKey, QString("%1 %2 %3 %4 %5 %6")
                          .arg(target_name(a.target))
                          .arg(a.start, 0, 16)
                          .arg(a.row)
                          .arg(a.width)
                          .arg(a.height)
                          .arg(where.last, 0, 16));
    return img;
}

std::optional<ImageArea> imageArea(const QImage &image)
{
    QStringList f = image.text(kKey).split(' ', Qt::SkipEmptyParts);
    if (f.size() != 6)
        return std::nullopt;
    auto t = parse_target(f[0].toStdString());
    bool ok[5];
    ImageArea out;
    out.area = { t.value_or(Target::WRAM), f[1].toUInt(&ok[0], 16), f[2].toUInt(&ok[1]), f[3].toUInt(&ok[2]),
                 f[4].toUInt(&ok[3]) };
    out.last = f[5].toUInt(&ok[4], 16);
    for (bool b : ok)
        if (!b)
            return std::nullopt;
    if (!t || !out.area.width || !out.area.height || out.area.width > out.area.row)
        return std::nullopt;
    return out;
}

std::optional<std::vector<std::pair<uint32_t, uint8_t>>>
imageToBytes(const QImage &image, const ImageArea &where, const std::vector<uint8_t> &bytes, QString *error)
{
    const Area &a = where.area;
    if (uint32_t(image.width()) != a.width || uint32_t(image.height()) != a.height)
    {
        if (error)
            *error = QObject::tr("The picture is %1x%2; it has to be %3x%4 to go back there")
                         .arg(image.width())
                         .arg(image.height())
                         .arg(a.width)
                         .arg(a.height);
        return std::nullopt;
    }
    QImage grey = image.convertToFormat(QImage::Format_Grayscale8);
    std::vector<std::pair<uint32_t, uint8_t>> out;
    for (uint32_t y = 0; y < a.height; y++)
    {
        const uchar *line = grey.constScanLine(int(y));
        for (uint32_t x = 0; x < a.width; x++)
        {
            uint32_t o = a.at(x, y);
            if (o > where.last || o >= bytes.size())
                continue;
            if (line[x] != bytes[o])
                out.push_back({ o, line[x] });
        }
    }
    return out;
}
