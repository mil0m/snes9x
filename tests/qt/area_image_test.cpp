#include "doctest/doctest.h"
#include "AreaImage.hpp"

#include <QColor>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>

using namespace corrupt;

namespace
{
std::vector<uint8_t> ramp(size_t n)
{
    std::vector<uint8_t> b(n);
    for (size_t i = 0; i < n; i++)
        b[i] = uint8_t(i * 3);
    return b;
}
} // namespace

TEST_SUITE("area pictures")
{
    TEST_CASE("a rectangle becomes a greyscale picture, one pixel a byte, and says where it came from")
    {
        auto bytes = ramp(0x1000);
        ImageArea where{ { Target::ROM, 0x100, 64, 8, 4 }, 0xFFF };
        QImage img = areaToImage(bytes, where);
        REQUIRE(img.width() == 8);
        REQUIRE(img.height() == 4);
        CHECK(img.format() == QImage::Format_Grayscale8);
        CHECK(qGray(img.pixel(0, 0)) == bytes[0x100]);
        CHECK(qGray(img.pixel(7, 3)) == bytes[0x100 + 3 * 64 + 7]);
        auto back = imageArea(img);
        REQUIRE(back);
        CHECK(back->area.target == Target::ROM);
        CHECK(back->area.start == 0x100);
        CHECK(back->area.row == 64);
        CHECK(back->area.width == 8);
        CHECK(back->area.height == 4);
    }

    TEST_CASE("the same picture back changes nothing; edited pixels become bytes to set")
    {
        auto bytes = ramp(0x1000);
        ImageArea where{ { Target::ROM, 0x100, 64, 8, 4 }, 0xFFF };
        QImage img = areaToImage(bytes, where);
        QString error;
        auto none = imageToBytes(img, where, bytes, &error);
        REQUIRE(none);
        CHECK(none->empty());

        img.setPixel(2, 1, qRgb(200, 200, 200));
        auto one = imageToBytes(img, where, bytes, &error);
        REQUIRE(one);
        REQUIRE(one->size() == 1);
        CHECK((*one)[0].first == 0x100 + 64 + 2);
        CHECK((*one)[0].second == 200);
    }

    TEST_CASE("a picture saved in colour reads as its brightness; a wrong size is refused")
    {
        auto bytes = ramp(0x1000);
        ImageArea where{ { Target::WRAM, 0, 16, 16, 2 }, 0xFFF };
        QImage colour = areaToImage(bytes, where).convertToFormat(QImage::Format_RGB32);
        colour.setPixel(0, 0, qRgb(255, 255, 255));
        QString error;
        auto diff = imageToBytes(colour, where, bytes, &error);
        REQUIRE(diff);
        REQUIRE(diff->size() == 1);
        CHECK((*diff)[0].second == 255);

        QImage wrong(15, 2, QImage::Format_Grayscale8);
        CHECK_FALSE(imageToBytes(wrong, where, bytes, &error));
        CHECK(error.contains("16"));
    }

    TEST_CASE("a range in rows: the last row's bytes past the range are left alone")
    {
        auto bytes = ramp(0x100);
        ImageArea where{ { Target::WRAM, 0x10, 16, 16, 3 }, 0x10 + 40 - 1 }; // 40 bytes in rows of 16
        QImage img = areaToImage(bytes, where);
        CHECK(img.height() == 3);
        CHECK(qGray(img.pixel(10, 2)) == 0); // past the range: black
        img.setPixel(10, 2, qRgb(99, 99, 99));
        img.setPixel(0, 2, qRgb(98, 98, 98));
        QString error;
        auto diff = imageToBytes(img, where, bytes, &error);
        REQUIRE(diff);
        REQUIRE(diff->size() == 1); // only the one inside
        CHECK((*diff)[0].first == 0x10 + 32);
    }

    TEST_CASE("the place survives a save to PNG and a load")
    {
        QTemporaryDir dir;
        auto bytes = ramp(0x2000);
        ImageArea where{ { Target::ROM, 0x1234, 32, 5, 7 }, 0x1FFF };
        QString path = dir.filePath("area.png");
        REQUIRE(areaToImage(bytes, where).save(path));
        QImage loaded(path);
        auto back = imageArea(loaded);
        REQUIRE(back);
        CHECK(back->area.start == 0x1234);
        CHECK(back->area.row == 32);
        CHECK(back->last == 0x1FFF);
        QString error;
        auto diff = imageToBytes(loaded, *back, bytes, &error);
        REQUIRE(diff);
        CHECK(diff->empty());
    }
}
