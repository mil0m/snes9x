#include "png_out.h"

#include <png.h>

#include <algorithm>
#include <cstdio>

using corrupt::Picture;

namespace headless
{

bool write_png(const std::string &path, const Picture &p)
{
    FILE *f = fopen(path.c_str(), "wb");
    if (!f)
        return false;
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    if (!png || !info || setjmp(png_jmpbuf(png)))
    {
        png_destroy_write_struct(&png, &info);
        fclose(f);
        return false;
    }
    png_init_io(png, f);
    png_set_IHDR(png, info, p.w, p.h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (int y = 0; y < p.h; y++)
        png_write_row(png, const_cast<png_bytep>(p.rgb.data() + size_t(y) * p.w * 3));
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    fclose(f);
    return true;
}

Picture strip_of(const std::vector<Picture> &ps)
{
    Picture out;
    if (ps.empty())
        return out;
    int gap = 4, w = 0, h = 0;
    for (auto &p : ps)
    {
        w += p.w + gap;
        h = std::max<int>(h, p.h);
    }
    out.w = uint16_t(w - gap);
    out.h = uint16_t(h);
    out.rgb.assign(size_t(out.w) * out.h * 3, 255);
    int x0 = 0;
    for (auto &p : ps)
    {
        for (int y = 0; y < p.h; y++)
            std::copy(p.rgb.begin() + size_t(y) * p.w * 3, p.rgb.begin() + size_t(y + 1) * p.w * 3,
                      out.rgb.begin() + (size_t(y) * out.w + x0) * 3);
        x0 += p.w + gap;
    }
    return out;
}

Picture grid_of(const std::vector<Picture> &ps, size_t per_row)
{
    std::vector<Picture> rows;
    for (size_t i = 0; i < ps.size(); i += per_row)
        rows.push_back(strip_of(std::vector<Picture>(ps.begin() + i, ps.begin() + std::min(ps.size(), i + per_row))));
    Picture out;
    if (rows.empty())
        return out;
    int gap = 4, w = 0, h = 0;
    for (auto &r : rows)
    {
        w = std::max<int>(w, r.w);
        h += r.h + gap;
    }
    out.w = uint16_t(w);
    out.h = uint16_t(h - gap);
    out.rgb.assign(size_t(out.w) * out.h * 3, 255);
    int y0 = 0;
    for (auto &r : rows)
    {
        for (int y = 0; y < r.h; y++)
            std::copy(r.rgb.begin() + size_t(y) * r.w * 3, r.rgb.begin() + size_t(y + 1) * r.w * 3,
                      out.rgb.begin() + size_t(y0 + y) * out.w * 3);
        y0 += r.h + gap;
    }
    return out;
}

} // namespace headless
