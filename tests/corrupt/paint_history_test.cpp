#include "doctest/doctest.h"

#include "corrupt/paint_history.h"

#include <map>
#include <set>

using namespace corrupt;

namespace
{

// A game's memories and a recipe's patches, as plain maps.
struct FakeStore : PaintStore
{
    std::map<std::pair<Target, uint32_t>, uint8_t> bytes;
    std::map<std::pair<Target, uint32_t>, uint8_t> patches;
    std::set<uint32_t> guarded; // ROM offsets paint must skip
    size_t writes = 0;

    size_t size(Target) const override { return 0x10000; }
    uint8_t read(Target t, uint32_t o) const override
    {
        auto it = bytes.find({ t, o });
        return it == bytes.end() ? 0 : it->second;
    }
    void write(Target t, uint32_t o, uint8_t v) override
    {
        bytes[{ t, o }] = v;
        writes++;
    }
    std::optional<uint8_t> patch(Target t, uint32_t o) const override
    {
        auto it = patches.find({ t, o });
        return it == patches.end() ? std::nullopt : std::optional<uint8_t>(it->second);
    }
    void set_patches(Target t, const std::vector<std::pair<uint32_t, std::optional<uint8_t>>> &list) override
    {
        for (auto [o, v] : list)
            if (v)
                patches[{ t, o }] = *v;
            else
                patches.erase({ t, o });
    }
    bool skip(Target t, uint32_t o) const override { return t == Target::ROM && guarded.count(o); }

    uint8_t rom(uint32_t o) const { return read(Target::ROM, o); }
    uint8_t wram(uint32_t o) const { return read(Target::WRAM, o); }
};

std::vector<std::pair<uint32_t, uint8_t>> bytes(std::initializer_list<uint32_t> offsets, uint8_t value)
{
    std::vector<std::pair<uint32_t, uint8_t>> out;
    for (auto o : offsets)
        out.push_back({ o, value });
    return out;
}

} // namespace

TEST_SUITE("paint history")
{
    TEST_CASE("ROM strokes go into the draft, never the game; later strokes win")
    {
        FakeStore store;
        PaintHistory h(store);
        h.dab(Target::ROM, bytes({ 0x10, 0x11 }, 0xEA));
        h.dab(Target::ROM, bytes({ 0x12 }, 0xEA)); // the same drag
        h.end_stroke();
        h.stroke(Target::ROM, bytes({ 0x11 }, 0x00));
        CHECK(store.writes == 0);
        auto draft = h.draft();
        CHECK(draft == std::map<uint32_t, uint8_t>{ { 0x10, 0xEA }, { 0x11, 0x00 }, { 0x12, 0xEA } });
        auto rows = h.rows();
        REQUIRE(rows.size() == 2);
        CHECK(rows[0].kind == PaintEntry::Draft); // newest first
        CHECK(rows[0].bytes == 1);
        CHECK(rows[1].bytes == 3);
        CHECK(rows[1].first == 0x10);
        CHECK(rows[1].value == 0xEA);
    }

    TEST_CASE("undo and redo strokes; a new stroke drops what could be redone")
    {
        FakeStore store;
        PaintHistory h(store);
        h.stroke(Target::ROM, bytes({ 1 }, 1));
        h.stroke(Target::ROM, bytes({ 2 }, 2));
        h.stroke(Target::ROM, bytes({ 3 }, 3));
        CHECK(h.undo());
        CHECK(h.undo());
        CHECK(h.draft().size() == 1);
        CHECK(h.can_redo());
        CHECK(h.redo());
        CHECK(h.draft().size() == 2);
        CHECK(h.rows().size() == 3); // the undone one is still listed, as redoable
        CHECK_FALSE(h.rows()[0].active);
        h.stroke(Target::ROM, bytes({ 9 }, 9));
        CHECK_FALSE(h.can_redo());
        CHECK(h.rows().size() == 3);
        CHECK(h.draft() == std::map<uint32_t, uint8_t>{ { 1, 1 }, { 2, 2 }, { 9, 9 } });
        h.go_to(1); // just after the first stroke
        CHECK(h.draft() == std::map<uint32_t, uint8_t>{ { 1, 1 } });
        h.go_to(3);
        CHECK(h.draft().size() == 3);
        int undone = 0;
        while (h.undo())
            undone++;
        CHECK(undone == 3); // three, then nothing left
    }

    TEST_CASE("Apply writes the draft as one batch, with patches; undo takes it out and back to the draft")
    {
        FakeStore store;
        store.bytes[{ Target::ROM, 0x20 }] = 0xA9;
        store.bytes[{ Target::ROM, 0x21 }] = 0x05;
        store.patches[{ Target::ROM, 0x21 }] = 0x07; // the recipe already painted this one
        PaintHistory h(store);
        h.stroke(Target::ROM, bytes({ 0x20 }, 0xEA));
        h.stroke(Target::ROM, bytes({ 0x21 }, 0xEA));
        auto applied = h.apply();
        REQUIRE(applied);
        CHECK(applied->bytes == 2);
        CHECK(store.rom(0x20) == 0xEA);
        CHECK(store.rom(0x21) == 0xEA);
        CHECK(store.patch(Target::ROM, 0x20) == 0xEA);
        CHECK(h.draft().empty());
        auto rows = h.rows();
        REQUIRE(rows.size() == 1);
        CHECK(rows[0].kind == PaintEntry::Applied);
        CHECK(rows[0].strokes == 2);
        CHECK(rows[0].bytes == 2);
        CHECK_FALSE(h.apply()); // nothing drafted

        CHECK(h.undo());
        CHECK(store.rom(0x20) == 0xA9);
        CHECK(store.rom(0x21) == 0x05);
        CHECK_FALSE(store.patch(Target::ROM, 0x20));
        CHECK(store.patch(Target::ROM, 0x21) == 0x07); // the recipe's own patch is back
        CHECK(h.draft() == std::map<uint32_t, uint8_t>{ { 0x20, 0xEA }, { 0x21, 0xEA } });
        CHECK(h.rows().size() == 2); // its two strokes, drafted again
        CHECK(h.undo());
        CHECK(h.draft().size() == 1);
    }

    TEST_CASE("Apply skips guarded bytes and says how many")
    {
        FakeStore store;
        store.guarded = { 0x31 };
        PaintHistory h(store);
        h.stroke(Target::ROM, bytes({ 0x30, 0x31 }, 0xFF));
        auto applied = h.apply();
        REQUIRE(applied);
        CHECK(applied->bytes == 1);
        CHECK(applied->skipped == 1);
        CHECK(store.rom(0x31) == 0);
    }

    TEST_CASE("RAM strokes are live: written as you drag, undo writes the old values back")
    {
        FakeStore store;
        store.bytes[{ Target::WRAM, 5 }] = 0x40;
        PaintHistory h(store);
        h.dab(Target::WRAM, bytes({ 5 }, 0x63));
        CHECK(store.wram(5) == 0x63); // at once
        h.dab(Target::WRAM, bytes({ 5, 6 }, 0x10)); // over itself in the same drag
        h.end_stroke();
        CHECK(store.wram(5) == 0x10);
        CHECK(store.patch(Target::WRAM, 5) == 0x10);
        CHECK(h.draft().empty());
        CHECK(h.rows()[0].kind == PaintEntry::Live);
        CHECK(h.undo());
        CHECK(store.wram(5) == 0x40); // the value before the stroke, not the first dab's
        CHECK(store.wram(6) == 0);
        CHECK_FALSE(store.patch(Target::WRAM, 5));
        CHECK(h.redo());
        CHECK(store.wram(5) == 0x10);
        CHECK(store.patch(Target::WRAM, 6) == 0x10);
    }

    TEST_CASE("throwing the draft away can be undone")
    {
        FakeStore store;
        PaintHistory h(store);
        h.stroke(Target::ROM, bytes({ 1, 2 }, 7));
        h.discard_draft();
        CHECK(h.draft().empty());
        CHECK(h.rows()[0].kind == PaintEntry::Discarded);
        CHECK(h.undo());
        CHECK(h.draft().size() == 2);
    }

    TEST_CASE("a crash after Apply: the batch comes out and its strokes go back to the draft")
    {
        FakeStore store;
        PaintHistory h(store);
        h.stroke(Target::ROM, bytes({ 0x40 }, 0xDB)); // STP
        auto applied = h.apply();
        REQUIRE(applied);
        h.stroke(Target::ROM, bytes({ 0x50 }, 1)); // drawn while it ran
        h.stroke(Target::WRAM, bytes({ 3 }, 9));   // live, undone by the restore point anyway
        CHECK(h.take_back(applied->entry));
        CHECK(store.rom(0x40) == 0);
        CHECK_FALSE(store.patch(Target::ROM, 0x40));
        CHECK(h.draft() == std::map<uint32_t, uint8_t>{ { 0x40, 0xDB }, { 0x50, 1 } });
        for (auto &row : h.rows())
            CHECK(row.kind == PaintEntry::Draft); // the live stroke went with the state it was in
        CHECK_FALSE(h.take_back(applied->entry)); // already out
    }

    TEST_CASE("clear forgets everything (another game or recipe)")
    {
        FakeStore store;
        PaintHistory h(store);
        h.stroke(Target::ROM, bytes({ 1 }, 1));
        h.dab(Target::ROM, bytes({ 2 }, 2)); // a drag still open
        h.clear();
        CHECK(h.rows().empty());
        CHECK(h.draft().empty());
        CHECK_FALSE(h.undo());
    }
}
