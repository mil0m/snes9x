#include "doctest/doctest.h"
#include "corrupt/envelope.h"

#include <set>

using namespace corrupt;

namespace
{

Picture flat(uint8_t r, uint8_t g, uint8_t b)
{
    Picture p;
    p.w = kThumbW;
    p.h = kThumbH;
    for (int i = 0; i < kThumbW * kThumbH; i++)
        p.rgb.insert(p.rgb.end(), { r, g, b });
    return p;
}

// A scene with a block at `at`.
Picture block_at(int at, uint8_t r = 255)
{
    Picture p = flat(20, 40, 60);
    for (int y = 0; y < 10; y++)
        for (int x = at; x < at + 8 && x < kThumbW; x++)
        {
            uint8_t *c = &p.rgb[(size_t(y) * kThumbW + x) * 3];
            c[0] = r, c[1] = 255, c[2] = 255;
        }
    return p;
}

RunRecord run_of(int frames, int start, uint8_t r = 255)
{
    RunRecord out;
    for (int f = 0; f < frames; f++)
    {
        out.thumbs.push_back(block_at((start + f) % 40, r));
        out.hashes.push_back(picture_hash(out.thumbs.back()));
    }
    out.frames = uint32_t(frames);
    return out;
}

// WRAM samples of 0x200 bytes: byte 0x100 takes `steady`, byte 0x180 `moving` (+ i), the rest 0.
StateTrace trace_of(int samples, uint8_t steady, uint8_t moving)
{
    StateTrace t;
    for (int i = 0; i < samples; i++)
    {
        std::vector<uint8_t> w(0x200, 0);
        w[0x100] = steady;
        w[0x180] = uint8_t(moving + i);
        t.wram.push_back(w);
    }
    return t;
}

std::vector<uint8_t> oam_with(std::initializer_list<std::array<int, 3>> sprites) // x, y, tile
{
    std::vector<uint8_t> o(kOamSize, 0);
    for (int i = 0; i < 128; i++)
        o[size_t(i) * 4 + 1] = 240; // off screen
    int i = 0;
    for (auto [x, y, tile] : sprites)
    {
        o[size_t(i) * 4] = uint8_t(x);
        o[size_t(i) * 4 + 1] = uint8_t(y);
        o[size_t(i) * 4 + 2] = uint8_t(tile);
        i++;
    }
    return o;
}

std::vector<Pads> buttons(int frames)
{
    auto p = std::vector<Pads>(size_t(frames));
    for (int f = 0; f < frames; f++)
        p[size_t(f)][0] = (f / 30) % 2 ? 0x0080 : 0x0100; // A, then right, every half second
    return p;
}

} // namespace

TEST_CASE("perturb: one small change early in the window, never Start or Select, the same per seed")
{
    auto pads = buttons(1200);
    std::set<std::string> kinds;
    for (uint64_t seed = 1; seed <= 60; seed++)
    {
        std::string what;
        auto p = perturb(pads, seed, &what);
        REQUIRE(p.size() == pads.size());
        CHECK(p == perturb(pads, seed));
        CHECK(p != pads);
        CHECK_FALSE(what.empty());
        kinds.insert(what.substr(0, what.find(' ')));
        size_t first = 0, last = 0;
        bool any = false;
        for (size_t f = 0; f < p.size(); f++)
        {
            CHECK((p[f][0] & 0x3000) == (pads[f][0] & 0x3000));
            if (p[f] != pads[f])
            {
                if (!any)
                    first = f;
                last = f;
                any = true;
            }
        }
        CHECK(first >= 1200 / 20 - 4);
        CHECK(first < 1200 / 4 + 4);
        CHECK(last - first < 60); // one press, not the rest of the window
    }
    CHECK(kinds.size() == 3); // shifted, dropped, added
}

TEST_CASE("perturb: buttons with no presses at all still get one added")
{
    std::vector<Pads> none(600);
    auto p = perturb(none, 3);
    CHECK(p != none);
}

TEST_CASE("sprite jumps: walking isn't jumping, teleporting is, appearing and flicker aren't")
{
    std::vector<std::vector<uint8_t>> walk;
    for (int f = 0; f < 10; f++)
        walk.push_back(oam_with({ { 10 + 2 * f, 100, 5 }, { 200, 50, 9 } }));
    CHECK(sprite_jump_rate(walk) == 0);

    auto tele = walk;
    tele[5] = oam_with({ { 150, 20, 5 }, { 200, 50, 9 } }); // jumps there and back
    CHECK(sprite_jump_rate(tele) == doctest::Approx(2.0 / 18)); // 2 sprites x 9 moves, 2 of them jumps

    auto swapped = walk; // the same sprites in other slots (a game's flicker)
    swapped[4] = oam_with({ { 200, 50, 9 }, { 18, 100, 5 } });
    CHECK(sprite_jump_rate(swapped) == 0);

    auto appear = walk; // a new tile turns up far away: not a jump
    appear[6] = oam_with({ { 22, 100, 5 }, { 200, 50, 9 }, { 120, 150, 33 } });
    CHECK(sprite_jump_rate(appear) == 0);

    auto off = walk; // off screen (y 240) doesn't count
    off[3] = oam_with({ { 16, 240, 5 }, { 200, 50, 9 } });
    CHECK(sprite_jump_rate(off) == 0);
}

TEST_CASE("envelope: a byte steady in normal play taking a new value is broken; one the run writes isn't")
{
    Envelope env;
    env.add(run_of(40, 0), trace_of(4, 1, 10));
    env.add(run_of(40, 3), trace_of(4, 2, 20));
    env.finish({});
    CHECK(env.runs() == 2);

    CHECK(env.measure(run_of(40, 1), trace_of(4, 2, 15)).steady_broken == 0);
    auto broken = env.measure(run_of(40, 1), trace_of(4, 7, 15));
    CHECK(broken.steady_broken == 1);
    std::vector<AddressRange> own{ { Target::WRAM, 0x0F0, 0x10F } };
    CHECK(env.measure(run_of(40, 1), trace_of(4, 7, 15), own).steady_broken == 0);
    std::vector<AddressRange> elsewhere{ { Target::VRAM, 0x0F0, 0x10F } }; // another memory
    CHECK(env.measure(run_of(40, 1), trace_of(4, 7, 15), elsewhere).steady_broken == 1);

    // Bytes the game never reads don't count.
    Envelope only_reads;
    only_reads.add(run_of(40, 0), trace_of(4, 1, 10));
    only_reads.add(run_of(40, 3), trace_of(4, 2, 20));
    std::vector<uint32_t> reads(0x200, 0);
    reads[0x180] = 5;
    only_reads.finish(reads);
    CHECK(only_reads.measure(run_of(40, 1), trace_of(4, 7, 15)).steady_broken == 0);
}

TEST_CASE("envelope: a byte that varies goes out of range only well beyond it")
{
    Envelope env;
    env.add(run_of(40, 0), trace_of(4, 1, 10)); // 10..13
    env.add(run_of(40, 3), trace_of(4, 1, 40)); // 40..43: range 10..43, a quarter is 8
    env.finish({});
    CHECK(env.measure(run_of(40, 1), trace_of(4, 1, 45)).out_of_range == 0); // up to 48
    CHECK(env.measure(run_of(40, 1), trace_of(4, 1, 60)).out_of_range == 1);
    CHECK(env.measure(run_of(40, 1), trace_of(4, 1, 0)).out_of_range == 1); // down to 0 (below 2)
}

TEST_CASE("envelope: new and silenced functions")
{
    Envelope env;
    StateTrace a = trace_of(2, 1, 1), b = trace_of(2, 1, 1);
    a.calls = { { 0x8000, 100 }, { 0x9000, 60 }, { 0xA000, 5 } };
    b.calls = { { 0x8000, 100 }, { 0x9000, 50 } }; // 0xA000 not in every run
    env.add(run_of(40, 0), a);
    env.add(run_of(40, 0), b);
    env.finish({});

    StateTrace same = trace_of(2, 1, 1);
    same.calls = { { 0x8000, 100 }, { 0x9000, 55 } };
    auto s = env.measure(run_of(40, 0), same);
    CHECK(s.new_functions == 0);
    CHECK(s.silenced == 0);

    StateTrace odd = trace_of(2, 1, 1);
    odd.calls = { { 0x8000, 100 }, { 0x9000, 20 }, { 0xB000, 3 } }; // 0xA000 gone: it wasn't in every run
    s = env.measure(run_of(40, 0), odd);
    CHECK(s.new_functions == 1);
    CHECK(s.silenced == 1);
}

TEST_CASE("envelope: apart is against the nearest natural run; colours never shown are new")
{
    Envelope env;
    env.add(run_of(40, 0), {});
    env.add(run_of(40, 20), {});
    env.finish({});
    CHECK(env.measure(run_of(40, 20), {}).apart == doctest::Approx(0));
    CHECK(env.measure(run_of(40, 20), {}).new_colours == doctest::Approx(0));
    auto far = env.measure(run_of(40, 10), {});
    CHECK(far.apart > 0);
    CHECK(far.new_colours == doctest::Approx(0)); // the same colours, elsewhere
    auto red = env.measure(run_of(40, 20, 0), {}); // the block in a colour never seen
    CHECK(red.new_colours == doctest::Approx(80.0 / (kThumbW * kThumbH)));
}
