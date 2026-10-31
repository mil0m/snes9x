#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/area_ops.h"
#include "corrupt/crash_map.h"
#include "corrupt/rule.h"

#include <string>

using namespace corrupt;

namespace
{

// A 4x3 rectangle at 0x100 in rows of 8 bytes, filled 1..12 left to right, top to bottom;
// the bytes around it are 0xEE so stray writes show.
struct Grid
{
    FakeMemory mem;
    Area area{ Target::WRAM, 0x100, 8, 4, 3 };

    Grid()
    {
        for (uint32_t a = 0xF0; a < 0x130; a++)
            mem.of(Target::WRAM)[a] = 0xEE;
        for (uint32_t k = 0; k < area.cells(); k++)
            mem.of(Target::WRAM)[area.cell(k)] = uint8_t(k + 1);
    }

    // The rectangle row by row, as "1 2 3 4 / 5 6 7 8 / 9 10 11 12".
    std::string rows() const
    {
        std::string out;
        for (uint32_t y = 0; y < area.height; y++)
        {
            if (y)
                out += " / ";
            for (uint32_t x = 0; x < area.width; x++)
                out += (x ? " " : "") + std::to_string(mem.bytes[size_t(Target::WRAM)][area.at(x, y)]);
        }
        return out;
    }

    bool outside_untouched() const
    {
        for (uint32_t a = 0xF0; a < 0x130; a++)
        {
            bool inside = a >= area.start && (a - area.start) % area.row < area.width &&
                          (a - area.start) / area.row < area.height;
            if (!inside && mem.bytes[size_t(Target::WRAM)][a] != 0xEE)
                return false;
        }
        return true;
    }

    std::vector<WriteEvent> apply(Op op, const HotMask *skip = nullptr)
    {
        Rng rng(1);
        return apply_area_op(op, area, mem, rng, skip);
    }
};

} // namespace

TEST_SUITE("areas")
{
    TEST_CASE("cells run left to right, top to bottom, in the row width")
    {
        Area a{ Target::WRAM, 0x100, 8, 4, 3 };
        CHECK(a.cells() == 12);
        CHECK(a.cell(0) == 0x100);
        CHECK(a.cell(3) == 0x103);
        CHECK(a.cell(4) == 0x108);
        CHECK(a.last() == 0x113);
        Area line = area_of({ Target::ROM, 0x40, 0x4F }, std::nullopt);
        CHECK(line.width == 16);
        CHECK(line.height == 1);
        CHECK(line.cell(15) == 0x4F);
        Area rect = area_of({ Target::ROM, 0x40, 0x9999 }, Rect{ 64, 8, 2 });
        CHECK(rect.last() == 0x40 + 64 + 7);
    }

    TEST_CASE("shift moves the region, wrapping around by default")
    {
        Grid g;
        g.apply({ .type = OpType::Shift, .dx = 1 });
        CHECK(g.rows() == "4 1 2 3 / 8 5 6 7 / 12 9 10 11");
        CHECK(g.outside_untouched());

        Grid down;
        down.apply({ .type = OpType::Shift, .dx = -1, .dy = 1 });
        CHECK(down.rows() == "10 11 12 9 / 2 3 4 1 / 6 7 8 5");
    }

    TEST_CASE("shift without wrapping fills what it uncovers")
    {
        Grid g;
        g.apply({ .type = OpType::Shift, .value = uint8_t(0x99), .dx = 2, .wrap = false });
        CHECK(g.rows() == "153 153 1 2 / 153 153 5 6 / 153 153 9 10");
        Grid far;
        far.apply({ .type = OpType::Shift, .dy = 7, .wrap = false });
        CHECK(far.rows() == "0 0 0 0 / 0 0 0 0 / 0 0 0 0");
    }

    TEST_CASE("a plain range is one row: shift by X is a byte offset")
    {
        FakeMemory mem;
        for (uint32_t i = 0; i < 6; i++)
            mem.of(Target::ROM)[0x10 + i] = uint8_t(i + 1);
        Rng rng(1);
        apply_area_op({ .type = OpType::Shift, .dx = 2 }, area_of({ Target::ROM, 0x10, 0x15 }, std::nullopt), mem, rng,
                      nullptr);
        auto &r = mem.of(Target::ROM);
        CHECK(std::vector<uint8_t>(r.begin() + 0x10, r.begin() + 0x16) == std::vector<uint8_t>{ 5, 6, 1, 2, 3, 4 });
    }

    TEST_CASE("flip mirrors rows, columns or both")
    {
        Grid h, v, both;
        h.apply({ .type = OpType::Flip, .axis = Axis::Horizontal });
        v.apply({ .type = OpType::Flip, .axis = Axis::Vertical });
        both.apply({ .type = OpType::Flip, .axis = Axis::Both });
        CHECK(h.rows() == "4 3 2 1 / 8 7 6 5 / 12 11 10 9");
        CHECK(v.rows() == "9 10 11 12 / 5 6 7 8 / 1 2 3 4");
        CHECK(both.rows() == "12 11 10 9 / 8 7 6 5 / 4 3 2 1");
        CHECK(both.outside_untouched());
    }

    TEST_CASE("levels remap every value through a curve")
    {
        auto one = [](Curve c, double amount, uint8_t v) {
            FakeMemory mem;
            mem.of(Target::WRAM)[0] = v;
            Rng rng(1);
            apply_area_op({ .type = OpType::Levels, .curve = c, .amount = amount },
                          area_of({ Target::WRAM, 0, 0 }, std::nullopt), mem, rng, nullptr);
            return int(mem.of(Target::WRAM)[0]);
        };
        CHECK(one(Curve::Invert, 0, 10) == 245);
        CHECK(one(Curve::Posterize, 2, 100) == 0);
        CHECK(one(Curve::Posterize, 2, 200) == 255);
        CHECK(one(Curve::Posterize, 3, 100) == 128);
        CHECK(one(Curve::Threshold, 128, 127) == 0);
        CHECK(one(Curve::Threshold, 128, 128) == 255);
        CHECK(one(Curve::Gamma, 2.0, 64) > 64); // above 1 brightens
        CHECK(one(Curve::Gamma, 0.5, 64) < 64);
        CHECK(one(Curve::Gamma, 2.0, 0) == 0);
        CHECK(one(Curve::Gamma, 2.0, 255) == 255);
        CHECK(one(Curve::Contrast, 0.5, 160) > 160);
        CHECK(one(Curve::Contrast, -0.5, 160) < 160);
        CHECK(one(Curve::Contrast, 0.5, 128) == 128);
    }

    TEST_CASE("filters work in two dimensions, with the edges repeated")
    {
        auto run = [](Kernel k, std::vector<uint8_t> nine) {
            FakeMemory mem;
            Area a{ Target::WRAM, 0, 3, 3, 3 };
            for (uint32_t i = 0; i < 9; i++)
                mem.of(Target::WRAM)[a.cell(i)] = nine[i];
            Rng rng(1);
            apply_area_op({ .type = OpType::Filter, .kernel = k }, a, mem, rng, nullptr);
            std::vector<int> out;
            for (uint32_t i = 0; i < 9; i++)
                out.push_back(mem.of(Target::WRAM)[a.cell(i)]);
            return out;
        };
        std::vector<uint8_t> flat(9, 77), spike{ 0, 0, 0, 0, 90, 0, 0, 0, 0 };
        CHECK(run(Kernel::Blur, flat) == std::vector<int>(9, 77));
        CHECK(run(Kernel::Blur, spike)[4] == 10);
        CHECK(run(Kernel::Blur, spike)[0] == 10); // every cell's 3x3 reaches the middle
        CHECK(run(Kernel::Median, spike) == std::vector<int>(9, 0));
        CHECK(run(Kernel::Edges, flat) == std::vector<int>(9, 0));
        CHECK(run(Kernel::Edges, spike)[4] == 255);
        CHECK(run(Kernel::Sharpen, flat) == std::vector<int>(9, 77));
        CHECK(run(Kernel::Max, spike) == std::vector<int>(9, 90));
        CHECK(run(Kernel::Min, flat) == std::vector<int>(9, 77));
        CHECK(run(Kernel::Emboss, flat) == std::vector<int>(9, 128));
    }

    TEST_CASE("stamp copies the same-sized region from elsewhere; fill sets every byte")
    {
        Grid g;
        for (uint32_t k = 0; k < 12; k++)
            g.mem.of(Target::WRAM)[0x200 + (k / 4) * 8 + k % 4] = uint8_t(100 + k);
        g.apply({ .type = OpType::Stamp, .source = 0x200 });
        CHECK(g.rows() == "100 101 102 103 / 104 105 106 107 / 108 109 110 111");
        CHECK(g.outside_untouched());

        Grid f;
        auto events = f.apply({ .type = OpType::Fill, .value = uint8_t(7) });
        CHECK(f.rows() == "7 7 7 7 / 7 7 7 7 / 7 7 7 7");
        CHECK(events.size() == 11); // the cell that already held 7 isn't a write
    }

    TEST_CASE("locked or avoided bytes are skipped, and only real changes are logged")
    {
        Grid g;
        HotMask skip;
        skip.add_block(Target::WRAM, 0x100); // 0x100-0x10F: the rectangle's first two rows (rows are 8 apart)
        auto events = g.apply({ .type = OpType::Fill, .value = uint8_t(0) }, &skip);
        CHECK(g.rows() == "1 2 3 4 / 5 6 7 8 / 0 0 0 0");
        CHECK(events.size() == 4);
    }

    TEST_CASE("per-byte ops on a rectangle only pick bytes inside it")
    {
        FakeMemory mem;
        Rule r;
        r.range = { Target::WRAM, 0x100, 0x100 };
        r.rect = Rect{ 8, 4, 3 };
        r.range.last = area_of(r.range, r.rect).last();
        r.op = { .type = OpType::Random, .min = 1, .max = 255 };
        r.when = { .type = TriggerType::EveryFrames, .every = 1 };
        r.count = 12; // all of it
        RuleRuntime rt;
        auto events = tick_rule(r, rt, mem, 5, 0, 1, 0);
        CHECK(events.size() == 12);
        for (auto &e : events)
        {
            uint32_t d = e.offset - 0x100;
            CHECK(d % 8 < 4);
            CHECK(d / 8 < 3);
        }
    }

    TEST_CASE("validation: the rectangle fits the target, and the op's settings make sense")
    {
        FakeMemory mem;
        size_t wram = mem.size(Target::WRAM);
        CHECK_FALSE(validate_area({ Target::WRAM, 0x100, 8, 4, 3 }, mem));
        CHECK(validate_area({ Target::WRAM, uint32_t(wram - 8), 8, 4, 3 }, mem)); // runs off the end
        CHECK(validate_area({ Target::WRAM, 0, 4, 8, 3 }, mem));                  // wider than its rows
        CHECK(validate_area({ Target::WRAM, 0, 8, 0, 3 }, mem));
        Area a{ Target::WRAM, 0x100, 8, 4, 3 };
        CHECK_FALSE(validate_area_op({ .type = OpType::Shift, .dx = -3 }, a, mem));
        CHECK(validate_area_op({ .type = OpType::Levels, .curve = Curve::Posterize, .amount = 1 }, a, mem));
        CHECK(validate_area_op({ .type = OpType::Levels, .curve = Curve::Gamma, .amount = 0 }, a, mem));
        CHECK(validate_area_op({ .type = OpType::Stamp, .source = uint32_t(wram - 4) }, a, mem));
        CHECK_FALSE(validate_area_op({ .type = OpType::Stamp, .source = 0x200 }, a, mem));
        CHECK(is_area_op(OpType::Shift));
        CHECK_FALSE(is_area_op(OpType::Random));
    }
}
