#include "doctest/doctest.h"

#include "corrupt/engine.h"
#include "corrupt/function_change.h"
#include "corrupt/function_guard.h"
#include "corrupt/recipe_json.h"
#include "corrupt/rule_text.h"
#include "fake_memory.h"

#include <initializer_list>

using namespace corrupt;

namespace
{

// LoROM-like: bank $00 then $01, $8000-$FFFF each; the test marks instructions as run.
struct Game
{
    std::vector<uint8_t> rom = std::vector<uint8_t>(0x10000, 0xFF);
    std::vector<uint8_t> flags = std::vector<uint8_t>(0x10000, 0);
    RomAddresses where;

    Game()
    {
        for (uint32_t c = 0; c < 16; c++)
            where.chunks.push_back(((c / 8) << 16) | (0x8000 + (c % 8) * 0x1000));
    }
    uint32_t put(uint32_t at, std::initializer_list<uint8_t> bytes, uint8_t width_flag = 0)
    {
        uint32_t i = at;
        for (auto b : bytes)
        {
            rom[i] = b;
            flags[i] |= (i == at ? codeflag::Opcode : codeflag::Operand) | width_flag;
            i++;
        }
        return i;
    }
};

// A function with one of everything, at 0x100 ($00:8100).
Game everything()
{
    Game g;
    uint32_t o = g.put(0x100, { 0xA9, 0x04 }, codeflag::Op8);        // LDA #$04        number, 8-bit
    o = g.put(o, { 0x69, 0x34, 0x12 }, codeflag::Op16);             // ADC #$1234      number, 16-bit, or SBC
    o = g.put(o, { 0xC9, 0x10, 0xEA }, codeflag::Op8 | codeflag::Op16); // CMP # both widths: left alone
    g.flags[o - 1] |= codeflag::Opcode;                              // (the NOP after its 8-bit form)
    o = g.put(o, { 0xF0, 0x02 });                                    // BEQ +2          branch
    o = g.put(o, { 0xE6, 0x20 });                                    // INC $20         math: DEC
    o = g.put(o, { 0x18 });                                          // CLC             math: SEC
    o = g.put(o, { 0x20, 0x00, 0x90 });                              // JSR $9000       call, near
    o = g.put(o, { 0x22, 0x00, 0x90, 0x01 });                        // JSL $01:9000    call, long
    o = g.put(o, { 0xA5, 0x20 });                                    // LDA $20         nothing to change
    o = g.put(o, { 0xC2, 0x20 });                                    // REP #$20        nothing (not a number)
    g.put(o, { 0x60 });
    g.put(0x1000, { 0x60 });
    g.put(0x9000, { 0x6B });
    return g;
}

std::vector<ChangeOption> options_of(const Game &g, uint32_t entry)
{
    return change_options(function_body(g.rom, g.flags, entry, g.where), g.rom, g.flags, g.where);
}

} // namespace

TEST_SUITE("function changes")
{
    TEST_CASE("the options: numbers as they ran, branches, math pairs, calls with their targets")
    {
        auto g = everything();
        auto os = options_of(g, 0x100);
        REQUIRE(os.size() == 7);
        CHECK(os[0].kind == ChangeKind::Number);
        CHECK(os[0].width == 1);
        CHECK(os[0].value == 0x04);
        CHECK(os[0].other == 0); // LDA has no math partner
        CHECK(os[0].text == "LDA #$04");
        CHECK(os[1].kind == ChangeKind::Number);
        CHECK(os[1].width == 2);
        CHECK(os[1].value == 0x1234);
        CHECK(os[1].other == 0xE9); // SBC #
        CHECK(os[2].kind == ChangeKind::Branch);
        CHECK(os[2].at == 0x108);
        CHECK(os[3].kind == ChangeKind::Math);
        CHECK(os[3].other == 0xC6); // DEC $dp
        CHECK(os[4].kind == ChangeKind::Math);
        CHECK(os[4].other == 0x38); // SEC
        CHECK(os[5].kind == ChangeKind::Skip);
        CHECK(os[5].call == CallType::Near);
        CHECK(os[5].target == 0x1000);
        CHECK(os[6].call == CallType::Long);
        CHECK(os[6].target == 0x9000);
    }

    TEST_CASE("math partners keep the addressing mode")
    {
        CHECK(math_partner(0x69) == 0xE9); // ADC # -> SBC #
        CHECK(math_partner(0x7D) == 0xFD); // ADC abs,X -> SBC abs,X
        CHECK(math_partner(0x1A) == 0x3A); // INC A -> DEC A
        CHECK(math_partner(0xE8) == 0xCA); // INX -> DEX
        CHECK(math_partner(0x0A) == 0x4A); // ASL A -> LSR A
        CHECK(math_partner(0x2E) == 0x6E); // ROL abs -> ROR abs
        CHECK(math_partner(0x25) == 0x05); // AND dp -> ORA dp
        CHECK_FALSE(math_partner(0xA5));   // LDA
        CHECK_FALSE(math_partner(0x60));   // RTS
    }

    TEST_CASE("the bytes each change writes")
    {
        auto g = everything();
        auto os = options_of(g, 0x100);
        CHECK(number_bytes(os[0], 0x01) == std::vector<uint8_t>{ 0xA9, 0x01 });
        CHECK(number_bytes(os[1], 0xBEEF) == std::vector<uint8_t>{ 0x69, 0xEF, 0xBE });
        CHECK(branch_bytes(os[2], BranchTo::Opposite) == std::vector<uint8_t>{ 0xD0 });
        CHECK(branch_bytes(os[2], BranchTo::Always) == std::vector<uint8_t>{ 0x80 });
        CHECK(branch_bytes(os[2], BranchTo::Never) == std::vector<uint8_t>{ 0xEA, 0xEA });
        CHECK(math_bytes(os[3]) == std::vector<uint8_t>{ 0xC6 });
        CHECK(skip_bytes(os[5]) == std::vector<uint8_t>(3, 0xEA));
        CHECK(skip_bytes(os[6]) == std::vector<uint8_t>(4, 0xEA));
        // Near calls stay in their bank; long calls go anywhere.
        CHECK(redirect_bytes(os[5], 0x2000, g.where) == std::vector<uint8_t>{ 0x20, 0x00, 0xA0 });
        CHECK_FALSE(redirect_bytes(os[5], 0x9000, g.where)); // bank $01
        CHECK(redirect_bytes(os[6], 0x2000, g.where) == std::vector<uint8_t>{ 0x22, 0x00, 0xA0, 0x00 });

        auto c = make_change(0x100, os[1], ChangeKind::Number, number_bytes(os[1], 3), g.rom);
        CHECK(c.function == 0x100);
        CHECK(c.at == 0x102);
        CHECK(c.was == std::vector<uint8_t>{ 0x69, 0x34, 0x12 });
        CHECK(c.value == std::vector<uint8_t>{ 0x69, 0x03, 0x00 });
    }

    TEST_CASE("the engine toggles a change on its hotkey, every byte of it, and sets it on or off")
    {
        FakeMemory mem;
        auto &rom = mem.of(Target::ROM);
        rom[0x200] = 0x69;
        rom[0x201] = 0x01;
        Engine e;
        Recipe r;
        Change c;
        c.function = 0x1F0;
        c.at = 0x200;
        c.kind = ChangeKind::Number;
        c.value = { 0x69, 0x05 };
        c.was = { 0x69, 0x01 };
        c.hotkey = 4;
        r.changes.push_back(c);
        e.set_recipe(r);
        e.set_active(true);
        e.press_hotkey(4);
        e.on_frame(mem);
        CHECK(rom[0x201] == 0x05);
        CHECK(change_on(c, mem));
        e.press_hotkey(4);
        e.on_frame(mem);
        CHECK(rom[0x201] == 0x01);
        CHECK(e.event_log().empty()); // not a rule's write

        // Apply with another value while it's on: On writes it whatever is there.
        e.flip_bytes({ 0x200, { 0x69, 0x07 }, { 0x69, 0x01 }, Engine::Flip::How::On });
        e.on_frame(mem);
        CHECK(rom[0x201] == 0x07);
        // A toggle of the kept change now sees neither its value nor the clean bytes: left alone.
        e.press_hotkey(4);
        e.on_frame(mem);
        CHECK(rom[0x201] == 0x07);
        e.flip_bytes({ 0x200, { 0x69, 0x07 }, { 0x69, 0x01 }, Engine::Flip::How::Off });
        e.on_frame(mem);
        CHECK(rom[0x201] == 0x01);
    }

    TEST_CASE("changes start on with the run, unless another ROM version holds something else")
    {
        FakeMemory mem;
        auto &rom = mem.of(Target::ROM);
        rom[0x300] = 0xF0;
        rom[0x400] = 0x11; // not what the change was made on
        Engine e;
        Recipe r;
        r.changes.push_back({ .function = 0x2F0, .at = 0x300, .kind = ChangeKind::Branch, .value = { 0x80 }, .was = { 0xF0 } });
        r.changes.push_back({ .function = 0x3F0, .at = 0x400, .kind = ChangeKind::Math, .value = { 0xE9 }, .was = { 0x69 } });
        r.changes.push_back({ .function = 0x4F0,
                              .at = 0x500,
                              .kind = ChangeKind::Math,
                              .value = { 0xE9 },
                              .was = { 0x00 },
                              .start_on = false });
        e.set_recipe(r);
        auto applied = e.apply_changes(mem);
        CHECK(applied.written == 1);
        CHECK(applied.mismatched == 1);
        CHECK(rom[0x300] == 0x80);
        CHECK(rom[0x400] == 0x11);
        CHECK(rom[0x500] == 0x00);
    }

    TEST_CASE("changes in the recipe: JSON round trip and errors; add and remove")
    {
        Recipe r;
        r.name = "c";
        r.changes.push_back({ .function = 0x9000,
                              .at = 0x9012,
                              .kind = ChangeKind::Number,
                              .value = { 0x69, 0x03 },
                              .was = { 0x69, 0x01 },
                              .hotkey = 2,
                              .start_on = false,
                              .note = "moon jump" });
        r.changes.push_back({ .function = 0x9000, .at = 0x9020, .kind = ChangeKind::Redirect, .value = { 0x20, 0x00, 0xA0 }, .was = { 0x20, 0x00, 0x90 } });
        auto text = write_recipe(r);
        CHECK(text.find("\"kind\": \"redirect\"") != std::string::npos);
        CHECK(text.find("\"value\": \"6903\"") != std::string::npos);
        auto back = parse_recipe(text, {});
        REQUIRE(back.errors.empty());
        REQUIRE(back.recipe->changes.size() == 2);
        auto &c = back.recipe->changes[0];
        CHECK(c.function == 0x9000);
        CHECK(c.at == 0x9012);
        CHECK(c.value == std::vector<uint8_t>{ 0x69, 0x03 });
        CHECK(c.was == std::vector<uint8_t>{ 0x69, 0x01 });
        CHECK(c.hotkey == 2);
        CHECK_FALSE(c.start_on);
        CHECK(c.note == "moon jump");
        CHECK(back.recipe->changes[1].start_on);
        CHECK(write_recipe(*back.recipe) == text);

        for (auto bad : { R"([{"function": "0x1", "at": "0x2", "kind": "warp", "value": "01", "was": "02"}])",
                          R"([{"function": "0x1", "at": "0x2", "kind": "math", "value": "0102", "was": "02"}])",
                          R"([{"function": "0x1", "at": "0x2", "kind": "math", "value": "zz", "was": "02"}])",
                          R"([{"at": "0x2", "kind": "math", "value": "01", "was": "02"}])" })
        {
            auto p = parse_recipe(std::string(R"({"format": 1, "name": "x", "rules": [], "changes": )") + bad + "}", {});
            CAPTURE(bad);
            REQUIRE_FALSE(p.errors.empty());
            CHECK(p.errors[0].find("changes[0]") != std::string::npos);
        }

        Engine e;
        e.set_recipe({});
        e.add_change(r.changes[1]);
        e.add_change(r.changes[0]);
        e.add_change(r.changes[0]);
        REQUIRE(e.recipe().changes.size() == 2);
        CHECK(e.recipe().changes[0].at == 0x9012);
        e.remove_change(0x9012);
        CHECK(e.recipe().changes.size() == 1);
    }

    TEST_CASE("a change puts its function in play, and its bytes are your own")
    {
        Recipe r;
        r.changes.push_back({ .function = 0x9000, .at = 0x9012, .kind = ChangeKind::Number, .value = { 0x69, 0x03 }, .was = { 0x69, 0x01 } });
        auto in = functions_in_play(r, {});
        REQUIRE(in.size() == 1);
        CHECK(in[0].entry == 0x9000);
        auto own = own_bytes(r);
        CHECK(own.is_hot(Target::ROM, 0x9012));
        CHECK(own.is_hot(Target::ROM, 0x9013));
        CHECK_FALSE(own.is_hot(Target::ROM, 0x9014));
    }

    TEST_CASE("what the window did is your own too: going up doesn't undo a flip or a change you didn't keep")
    {
        // A function switched off from the window: its return is in its own guarded body.
        Recipe r;
        std::vector<uint8_t> clean(0x40, 0xEA), now = clean;
        now[0x10] = kRts;     // switched off from the window
        now[0x20] = 0x80;     // a branch made always, applied, not kept
        now[0x14] = 0x00;     // what other corruption did
        HotMask guarded;
        for (uint32_t o = 0x10; o < 0x28; o++)
            guarded.add(Target::ROM, o);
        Change c{ .function = 0x1C, .at = 0x20, .kind = ChangeKind::Branch, .value = { 0x80 }, .was = { 0xEA } };
        auto fix = guard_cleanup(guarded, own_bytes(r, { 0x10 }, { c }), clean, now);
        CHECK(fix == std::vector<std::pair<uint32_t, uint8_t>>{ { 0x14, 0xEA } });
        auto in = functions_in_play(r, { 0x10, c.function });
        CHECK(in.size() == 2);
    }
}

TEST_SUITE("function changes")
{
    TEST_CASE("aimed inside busy functions: only their code, taken once a run, nothing before it's known")
    {
        FakeMemory mem;
        std::vector<uint8_t> flags(mem.size(Target::ROM), 0);
        for (uint32_t o = 0x100; o < 0x200; o++)
            flags[o] = codeflag::Opcode;
        Rule r;
        r.range = { Target::ROM, 0x100, 0x1FF };
        r.op.type = OpType::Random;
        r.op.min = 1;
        r.op.max = 255;
        r.when.type = TriggerType::EveryFrames;
        r.when.every = 1;
        r.count = 8;
        r.aim.in_busy_functions = true;
        CHECK(describe_rule(r).find("inside busy functions") != std::string::npos);

        HotMask busy;
        for (uint32_t o = 0x140; o < 0x150; o++)
            busy.add(Target::ROM, o);
        CHECK(aim_rule(r, mem.of(Target::ROM), flags, &busy).size() == 16);
        CHECK(aim_rule(r, mem.of(Target::ROM), flags, nullptr).empty());

        Engine e;
        Recipe recipe;
        recipe.rules.push_back(r);
        e.set_recipe(recipe);
        e.set_code_flags([&] { return std::span<const uint8_t>(flags); });
        int asked = 0;
        bool known = false;
        e.set_busy_code([&] {
            asked++;
            return known ? busy : HotMask{};
        });
        e.set_active(true);
        e.on_frame(mem);
        CHECK(e.event_log().empty()); // nothing busy known yet
        known = true;
        for (int i = 0; i < 5; i++)
            e.on_frame(mem);
        CHECK(asked == 2); // asked again until it knew, then once for the run
        CHECK_FALSE(e.event_log().empty());
        for (auto &ev : e.event_log())
            CHECK(busy.is_hot(Target::ROM, ev.write.offset));

        auto text = write_recipe(recipe);
        CHECK(text.find("\"within\": \"busy_functions\"") != std::string::npos);
        auto back = parse_recipe(text, {});
        REQUIRE(back.errors.empty());
        CHECK(back.recipe->rules[0].aim.in_busy_functions);
        auto wrong = text;
        wrong.replace(wrong.find("busy_functions"), 14, "everywhere");
        auto bad = parse_recipe(wrong, {});
        REQUIRE_FALSE(bad.errors.empty());
        CHECK(bad.errors[0].find("within") != std::string::npos);
    }
}
