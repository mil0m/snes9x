#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/engine.h"

using namespace corrupt;

namespace
{
Recipe noisy_recipe(uint64_t seed)
{
    Recipe r;
    r.name = "noise";
    r.seed = seed;

    Rule noise;
    noise.name = "noise";
    noise.range = { Target::WRAM, 0x0000, 0xFFFF };
    noise.op = { .type = OpType::Random, .min = 0, .max = 255 };
    noise.when = { .type = TriggerType::ChancePerFrame, .chance = 0.2 };
    noise.count = 3;
    r.rules.push_back(noise);

    Rule drift;
    drift.name = "drift";
    drift.range = { Target::CGRAM, 0, 0x1FF };
    drift.op = { .type = OpType::Add, .min = -2, .max = 2 };
    drift.when = { .type = TriggerType::EveryFrames, .every = 30 };
    drift.count = 4;
    r.rules.push_back(drift);
    return r;
}

Rule hotkey_rule(int key)
{
    Rule r;
    r.name = "key";
    r.range = { Target::WRAM, 0x10, 0x10 };
    r.op = { .type = OpType::Add, .min = 1, .max = 1 };
    r.when = { .type = TriggerType::Hotkey, .hotkey = key };
    return r;
}

void frames(Engine &e, FakeMemory &mem, int n)
{
    for (int i = 0; i < n; i++)
        e.on_frame(mem);
}
} // namespace

TEST_CASE("the same recipe from the same memory replays identically")
{
    FakeMemory a, b;
    Engine ea, eb;
    ea.set_recipe(noisy_recipe(1337));
    eb.set_recipe(noisy_recipe(1337));
    ea.set_active(true);
    eb.set_active(true);
    frames(ea, a, 600);
    frames(eb, b, 600);
    CHECK(a.bytes == b.bytes);
    CHECK(a.writes > 0);
}

TEST_CASE("re-rolling changes the outcome and restarts the run")
{
    FakeMemory a, b;
    Engine ea, eb;
    ea.set_recipe(noisy_recipe(1337));
    eb.set_recipe(noisy_recipe(1337));
    ea.set_active(true);
    eb.set_active(true);
    frames(eb, b, 50);
    eb.reroll(4242);
    CHECK(eb.status().frame == 0);
    CHECK(eb.recipe().seed == 4242);

    // A rerolled run behaves like a fresh run with the new seed (modulo what the first 50 frames wrote).
    FakeMemory fresh;
    Engine ef;
    ef.set_recipe(noisy_recipe(4242));
    ef.set_active(true);
    FakeMemory b2 = fresh;
    Engine eb2;
    eb2.set_recipe(noisy_recipe(1337));
    eb2.set_active(true);
    eb2.reroll(4242);
    frames(ef, fresh, 300);
    frames(eb2, b2, 300);
    CHECK(fresh.bytes == b2.bytes);

    frames(ea, a, 300);
    CHECK(a.bytes != fresh.bytes);
}

TEST_CASE("an inactive engine writes nothing")
{
    FakeMemory mem;
    Engine e;
    e.set_recipe(noisy_recipe(1));
    frames(e, mem, 300);
    CHECK(mem.writes == 0);
    CHECK(e.status().frame == 0);
}

TEST_CASE("activation restarts the frame count")
{
    FakeMemory mem;
    Engine e;
    e.set_recipe(noisy_recipe(1));
    e.set_active(true);
    frames(e, mem, 10);
    CHECK(e.status().frame == 10);
    e.set_active(false);
    e.set_active(true);
    CHECK(e.status().frame == 0);
}

TEST_CASE("hotkey rules fire once per press, on the next frame")
{
    FakeMemory mem;
    Engine e;
    Recipe r;
    r.rules.push_back(hotkey_rule(2));
    e.set_recipe(r);
    e.set_active(true);

    frames(e, mem, 5);
    CHECK(mem.of(Target::WRAM)[0x10] == 0);

    e.press_hotkey(2);
    e.press_hotkey(5); // nothing bound
    frames(e, mem, 5);
    CHECK(mem.of(Target::WRAM)[0x10] == 1);
}

TEST_CASE("hotkey presses while inactive are dropped")
{
    FakeMemory mem;
    Engine e;
    Recipe r;
    r.rules.push_back(hotkey_rule(1));
    e.set_recipe(r);
    e.press_hotkey(1);
    e.set_active(true);
    frames(e, mem, 3);
    CHECK(mem.of(Target::WRAM)[0x10] == 0);
}

TEST_CASE("rules can be toggled live")
{
    FakeMemory mem;
    Engine e;
    Recipe r;
    Rule every = hotkey_rule(1);
    every.when = { .type = TriggerType::EveryFrames, .every = 1 };
    r.rules.push_back(every);
    e.set_recipe(r);
    e.set_active(true);
    frames(e, mem, 3);
    e.set_rule_enabled(0, false);
    frames(e, mem, 3);
    CHECK(mem.of(Target::WRAM)[0x10] == 3);
    CHECK_FALSE(e.recipe().rules[0].enabled);
}

TEST_CASE("the event log records frame, rule and write, and stays bounded")
{
    FakeMemory mem;
    Engine e;
    Recipe r;
    Rule every = hotkey_rule(1);
    every.range = { Target::WRAM, 0, 0xFFFF };
    every.when = { .type = TriggerType::EveryFrames, .every = 1 };
    every.count = 50;
    r.rules.push_back(every);
    e.set_recipe(r);
    e.set_active(true);

    frames(e, mem, 1);
    REQUIRE(e.event_log().size() == 50);
    CHECK(e.event_log().front().frame == 1);
    CHECK(e.event_log().front().rule_index == 0);

    frames(e, mem, 400); // 20k writes
    CHECK(e.event_log().size() == Engine::kEventLogLimit);
    CHECK(e.event_log().back().frame == 401);
}

TEST_CASE("status counts firings across rules")
{
    FakeMemory mem;
    Engine e;
    e.set_recipe(noisy_recipe(9));
    e.set_active(true);
    frames(e, mem, 90);
    auto s = e.status();
    CHECK(s.active);
    CHECK(s.frame == 90);
    CHECK(s.firings >= 3); // the every-30 drift alone
    CHECK(s.recipe_name == "noise");
}

TEST_CASE("a rule's salt changes only that rule's choices")
{
    FakeMemory plain, salted;
    Engine ep, es;
    auto recipe = noisy_recipe(77);
    ep.set_recipe(recipe);
    recipe.rules[0].salt = 12345;
    es.set_recipe(recipe);
    ep.set_active(true);
    es.set_active(true);
    frames(ep, plain, 300);
    frames(es, salted, 300);
    CHECK(plain.of(Target::WRAM) != salted.of(Target::WRAM));   // rule 0 (wram) re-rolled
    CHECK(plain.of(Target::CGRAM) == salted.of(Target::CGRAM)); // rule 1 (cgram) untouched
}

TEST_SUITE("rollback support")
{
    TEST_CASE("snapshot + restore replays the same frames")
    {
        FakeMemory mem;
        Engine e;
        e.set_recipe(noisy_recipe(5));
        e.set_active(true);
        frames(e, mem, 40);

        auto snap = e.snapshot();
        FakeMemory saved = mem;
        frames(e, mem, 100);
        auto first = mem.bytes;
        size_t log_after_first = e.event_log().size();

        e.restore(snap);
        mem.bytes = saved.bytes;
        CHECK(e.status().frame == 40);
        frames(e, mem, 100);
        CHECK(mem.bytes == first);
        CHECK(e.event_log().size() == log_after_first);
    }

    TEST_CASE("restore drops log entries from after the snapshot")
    {
        FakeMemory mem;
        Engine e;
        e.set_recipe(noisy_recipe(5));
        e.set_active(true);
        frames(e, mem, 30);
        auto snap = e.snapshot();
        frames(e, mem, 60);
        e.restore(snap);
        for (auto &ev : e.event_log())
            CHECK(ev.frame <= 30);
    }

    TEST_CASE("re-rolling one rule changes what happens after a restore")
    {
        FakeMemory mem;
        Engine e;
        e.set_recipe(noisy_recipe(5));
        e.set_active(true);
        auto snap = e.snapshot();
        FakeMemory saved = mem;
        frames(e, mem, 100);
        auto first = mem.bytes;

        e.restore(snap);
        mem.bytes = saved.bytes;
        e.reroll_rule(0, 999);
        CHECK(e.recipe().rules[0].salt == 999u);
        frames(e, mem, 100);
        CHECK(mem.of(Target::WRAM) != first[size_t(Target::WRAM)]);
        CHECK(mem.of(Target::CGRAM) == first[size_t(Target::CGRAM)]);
    }

    TEST_CASE("a muted rule stays quiet until its frame, then resumes")
    {
        FakeMemory mem;
        Engine e;
        Recipe r;
        Rule every = hotkey_rule(1);
        every.when = { .type = TriggerType::EveryFrames, .every = 1 };
        r.rules.push_back(every);
        e.set_recipe(r);
        e.set_active(true);
        e.mute_rule_until(0, 10);
        frames(e, mem, 10);
        CHECK(mem.of(Target::WRAM)[0x10] == 0);
        frames(e, mem, 5);
        CHECK(mem.of(Target::WRAM)[0x10] == 5);
    }

    TEST_CASE("mutes, salts and intensity survive a restore; a new recipe clears mutes")
    {
        FakeMemory mem;
        Engine e;
        e.set_recipe(noisy_recipe(5));
        e.set_active(true);
        auto snap = e.snapshot();
        e.mute_rule_until(0, 50);
        e.reroll_rule(1, 7);
        e.set_rule_intensity(1, 0.5);
        e.restore(snap);
        CHECK(e.muted_until(0) == 50u);
        CHECK(e.recipe().rules[1].salt == 7u);
        CHECK(e.recipe().rules[1].intensity == 0.5);
        e.set_recipe(noisy_recipe(5));
        CHECK(e.muted_until(0) == 0u);
    }

    TEST_CASE("restarting the run (reactivate, re-roll) clears mutes")
    {
        Engine e;
        e.set_recipe(noisy_recipe(5));
        e.set_active(true);
        e.mute_rule_until(0, 500);
        e.reroll(6);
        CHECK(e.muted_until(0) == 0u);
    }
}

TEST_SUITE("live rule edits")
{
    Rule noise_on(Target t, uint32_t last)
    {
        Rule r;
        r.name = target_name(t);
        r.range = { t, 0, last };
        r.op = { .type = OpType::Random, .min = 0, .max = 255 };
        r.when = { .type = TriggerType::EveryFrames, .every = 7 };
        r.count = 2;
        return r;
    }

    Rule sticky_in(uint32_t first, uint32_t last)
    {
        Rule r;
        r.name = "sticky";
        r.range = { Target::WRAM, first, last };
        r.op = { .type = OpType::Add, .min = 1, .max = 1 };
        r.when = { .type = TriggerType::EveryFrames, .every = 1 };
        r.count = 4;
        r.pick = Pick::Sticky;
        return r;
    }

    Recipe three_targets()
    {
        Recipe r;
        r.seed = 99;
        r.rules = { noise_on(Target::WRAM, 0xFFFF), noise_on(Target::CGRAM, 0x1FF), noise_on(Target::OAM, 0x21F) };
        return r;
    }

    // Offsets in WRAM that differ from zero.
    std::vector<uint32_t> touched_wram(FakeMemory & mem)
    {
        std::vector<uint32_t> out;
        auto &w = mem.of(Target::WRAM);
        for (uint32_t i = 0; i < w.size(); i++)
            if (w[i])
                out.push_back(i);
        return out;
    }

    TEST_CASE("replacing a rule leaves the other rules' runs exactly as they were")
    {
        FakeMemory plain, edited;
        Engine a, b;
        a.set_recipe(three_targets());
        b.set_recipe(three_targets());
        a.set_active(true);
        b.set_active(true);
        frames(a, plain, 150);
        frames(b, edited, 150);

        b.replace_rule(1, noise_on(Target::VRAM, 0xFFFF));
        CHECK(b.current_frame() == 150);
        CHECK(b.active());
        frames(a, plain, 150);
        frames(b, edited, 150);

        CHECK(plain.of(Target::WRAM) == edited.of(Target::WRAM));
        CHECK(plain.of(Target::OAM) == edited.of(Target::OAM));
        CHECK(plain.of(Target::CGRAM) != edited.of(Target::CGRAM)); // stopped at frame 150 in b
        CHECK(b.recipe().rules[1].range.target == Target::VRAM);
        bool vram_written = false;
        for (auto v : edited.of(Target::VRAM))
            vram_written |= v != 0;
        CHECK(vram_written);
    }

    TEST_CASE("a replaced sticky rule picks its addresses again")
    {
        FakeMemory mem;
        Engine e;
        Recipe r;
        r.rules = { sticky_in(0x100, 0x1FF) };
        e.set_recipe(r);
        e.set_active(true);
        frames(e, mem, 10);
        mem = FakeMemory{};
        e.replace_rule(0, sticky_in(0x300, 0x3FF));
        frames(e, mem, 10);
        auto touched = touched_wram(mem);
        CHECK(touched.size() == 4);
        for (auto a : touched)
            CHECK((a >= 0x300 && a <= 0x3FF));
    }

    TEST_CASE("replacing a rule gives it a new id and clears its mute")
    {
        Engine e;
        e.set_recipe(three_targets());
        e.set_active(true);
        auto before = e.rule_id(1);
        CHECK(before != e.rule_id(0));
        e.mute_rule_until(1, 500);
        e.replace_rule(1, noise_on(Target::VRAM, 0xFF));
        CHECK(e.rule_id(1) != before);
        CHECK(e.muted_until(1) == 0u);
    }

    TEST_CASE("adding a rule keeps the run going and the new rule fires")
    {
        FakeMemory mem;
        Engine e;
        Recipe r;
        r.rules = { noise_on(Target::CGRAM, 0x1FF) };
        e.set_recipe(r);
        e.set_active(true);
        frames(e, mem, 50);
        CHECK(e.add_rule(noise_on(Target::OAM, 0x21F)) == 1u);
        CHECK(e.current_frame() == 50);
        CHECK(e.recipe().rules.size() == 2);
        frames(e, mem, 20);
        bool oam_written = false;
        for (auto v : mem.of(Target::OAM))
            oam_written |= v != 0;
        CHECK(oam_written);
    }

    TEST_CASE("removing a rule drops its log entries and renumbers the rest")
    {
        FakeMemory mem;
        Engine e;
        e.set_recipe(three_targets());
        e.set_active(true);
        frames(e, mem, 60);
        auto oam_id = e.rule_id(2);
        e.remove_rule(1);
        CHECK(e.current_frame() == 60);
        CHECK(e.recipe().rules.size() == 2);
        CHECK(e.rule_id(1) == oam_id);
        bool saw_oam = false;
        for (auto &ev : e.event_log())
        {
            CHECK(ev.write.target != Target::CGRAM);
            CHECK(ev.rule_index < 2u);
            if (ev.write.target == Target::OAM)
            {
                saw_oam = true;
                CHECK(ev.rule_index == 1u);
            }
        }
        CHECK(saw_oam);
    }

    TEST_CASE("edits with an index out of range do nothing")
    {
        Engine e;
        e.set_recipe(three_targets());
        e.replace_rule(3, noise_on(Target::VRAM, 0xFF));
        e.remove_rule(7);
        CHECK(e.recipe().rules.size() == 3);
        CHECK(e.recipe().rules[2].range.target == Target::OAM);
    }

    TEST_CASE("restoring a snapshot from before a replace starts the replaced rule fresh")
    {
        FakeMemory mem;
        Engine e;
        Recipe r;
        r.rules = { sticky_in(0x100, 0x1FF) };
        e.set_recipe(r);
        e.set_active(true);
        frames(e, mem, 10); // sticky addresses picked in 0x100..0x1FF
        auto snap = e.snapshot();
        e.replace_rule(0, sticky_in(0x300, 0x3FF));
        e.restore(snap);
        CHECK(e.current_frame() == 10);
        mem = FakeMemory{};
        frames(e, mem, 10);
        auto touched = touched_wram(mem);
        CHECK(touched.size() == 4);
        for (auto a : touched)
            CHECK((a >= 0x300 && a <= 0x3FF));
    }

    TEST_CASE("restoring a snapshot from before an add or remove restores the rules that match")
    {
        FakeMemory mem;
        Engine e, reference;
        e.set_recipe(three_targets());
        reference.set_recipe(three_targets());
        e.set_active(true);
        reference.set_active(true);
        FakeMemory ref_mem;
        frames(e, mem, 40);
        frames(reference, ref_mem, 40);
        auto snap = e.snapshot();
        auto saved = mem;
        frames(e, mem, 40);

        e.remove_rule(1);
        e.add_rule(noise_on(Target::VRAM, 0xFFFF));
        e.restore(snap);
        mem = saved;
        CHECK(e.current_frame() == 40);
        frames(e, mem, 60);
        reference.remove_rule(1);
        frames(reference, ref_mem, 60);
        // Rule 2 moved to index 1, which changes its random stream; WRAM (rule 0) is untouched.
        CHECK(mem.of(Target::WRAM) == ref_mem.of(Target::WRAM));
    }
}

TEST_CASE("renaming the recipe keeps the run going")
{
    FakeMemory mem;
    Engine e;
    e.set_recipe(noisy_recipe(5));
    e.set_active(true);
    frames(e, mem, 30);
    e.set_info("renamed", "new notes");
    CHECK(e.recipe().name == "renamed");
    CHECK(e.recipe().notes == "new notes");
    CHECK(e.current_frame() == 30);
    CHECK(e.status().recipe_name == "renamed");
}

TEST_SUITE("recipe intensity")
{
    TEST_CASE("half recipe intensity is the same as every rule at half")
    {
        auto by_recipe = noisy_recipe(11);
        by_recipe.intensity = 0.5;
        auto by_rules = noisy_recipe(11);
        for (auto &r : by_rules.rules)
            r.intensity = 0.5;
        FakeMemory a, b, full;
        Engine ea, eb, ef;
        ea.set_recipe(by_recipe);
        eb.set_recipe(by_rules);
        ef.set_recipe(noisy_recipe(11));
        ea.set_active(true);
        eb.set_active(true);
        ef.set_active(true);
        frames(ea, a, 300);
        frames(eb, b, 300);
        frames(ef, full, 300);
        CHECK(a.bytes == b.bytes);
        CHECK(a.bytes != full.bytes);
    }

    TEST_CASE("intensity 0 stops every rule; changing it live keeps the run")
    {
        FakeMemory mem;
        Engine e;
        e.set_recipe(noisy_recipe(3));
        e.set_active(true);
        frames(e, mem, 30);
        e.set_intensity(0.0);
        CHECK(e.recipe().intensity == 0.0);
        CHECK(e.current_frame() == 30);
        auto before = mem.bytes;
        frames(e, mem, 300);
        CHECK(mem.bytes == before);
        e.set_intensity(1.0);
        frames(e, mem, 60);
        CHECK(mem.bytes != before);
    }

    TEST_CASE("recipe intensity is clamped to 0..1 and survives restores and re-rolls")
    {
        Engine e;
        e.set_recipe(noisy_recipe(3));
        e.set_active(true);
        auto snap = e.snapshot();
        e.set_intensity(1.7);
        CHECK(e.recipe().intensity == 1.0);
        e.set_intensity(-1);
        CHECK(e.recipe().intensity == 0.0);
        e.set_intensity(0.3);
        e.restore(snap);
        CHECK(e.recipe().intensity == 0.3);
        e.reroll(9);
        CHECK(e.recipe().intensity == 0.3);
    }
}

TEST_SUITE("held starts")
{
    TEST_CASE("by default a run starts on the next frame")
    {
        FakeMemory mem;
        Engine e;
        e.set_recipe(noisy_recipe(1));
        e.set_active(true);
        CHECK_FALSE(e.waiting());
        frames(e, mem, 5);
        CHECK(e.current_frame() == 5);
    }

    TEST_CASE("a held run writes nothing and stays at frame 0 until released")
    {
        FakeMemory mem;
        Engine e;
        e.hold_starts(true);
        e.set_recipe(noisy_recipe(1));
        e.set_active(true);
        CHECK(e.waiting());
        frames(e, mem, 50);
        CHECK(e.current_frame() == 0);
        CHECK(mem.writes == 0);
        e.release();
        CHECK_FALSE(e.waiting());
        frames(e, mem, 50);
        CHECK(e.current_frame() == 50);
        CHECK(mem.writes > 0);
    }

    TEST_CASE("every restart holds again; edits and rollbacks don't")
    {
        FakeMemory mem;
        Engine e;
        e.hold_starts(true);
        e.set_recipe(noisy_recipe(1));
        e.set_active(true);
        e.release();
        frames(e, mem, 10);
        auto snap = e.snapshot();
        e.add_rule(noisy_recipe(2).rules[0]);
        e.set_intensity(0.5);
        e.restore(snap);
        CHECK_FALSE(e.waiting());

        e.reroll(7);
        CHECK(e.waiting());
        e.release();
        e.set_recipe(noisy_recipe(3));
        CHECK(e.waiting());
        e.release();
        e.set_active(false);
        CHECK_FALSE(e.waiting()); // an inactive engine waits for nothing
        e.set_active(true);
        CHECK(e.waiting());
    }
}

TEST_CASE("naming the recipe's saved moment keeps the run going")
{
    FakeMemory mem;
    Engine e;
    e.set_recipe(noisy_recipe(5));
    e.set_active(true);
    frames(e, mem, 12);
    e.set_anchor_state("noise.anchor.frz");
    CHECK(e.recipe().anchor_state == "noise.anchor.frz");
    CHECK(e.current_frame() == 12);
}

TEST_CASE("renaming a region in the running recipe keeps the run going")
{
    FakeMemory mem;
    Engine e;
    auto r = noisy_recipe(5);
    r.rules[0].region = "stats";
    r.rules[1].region = "palette";
    e.set_recipe(r);
    e.set_active(true);
    frames(e, mem, 20);
    auto id = e.rule_id(0);
    e.rename_region("stats", "player stats");
    CHECK(e.recipe().rules[0].region == "player stats");
    CHECK(e.recipe().rules[1].region == "palette");
    CHECK(e.rule_id(0) == id); // not replaced: same run
    CHECK(e.current_frame() == 20);
    e.rename_region("palette", ""); // deleted: the rule keeps its range, unnamed
    CHECK(e.recipe().rules[1].region.empty());
    CHECK(e.recipe().rules[1].range.target == Target::CGRAM);
}

TEST_SUITE("at_load rules")
{
    Recipe classic()
    {
        Recipe r;
        r.name = "classic";
        r.seed = 9;
        Rule rom;
        rom.name = "rom";
        rom.range = { Target::ROM, 0x100, 0x1FF };
        rom.op = { .type = OpType::Random, .min = 1, .max = 255 };
        rom.when = { .type = TriggerType::AtLoad };
        rom.count = 8;
        Rule ram = rom;
        ram.name = "ram";
        ram.range = { Target::WRAM, 0x100, 0x1FF };
        Rule live = rom;
        live.name = "live";
        live.when = { .type = TriggerType::EveryFrames, .every = 1 };
        live.range = { Target::WRAM, 0x400, 0x4FF };
        r.rules = { rom, ram, live };
        return r;
    }

    TEST_CASE("they fire once at the start of a run, at frame 0, and never in a frame")
    {
        FakeMemory mem;
        Engine e;
        e.set_recipe(classic());
        e.set_active(true);
        CHECK(e.fire_at_load(mem) == 16); // both at_load rules, 8 bytes each
        REQUIRE(e.event_log().size() == 16);
        for (auto &ev : e.event_log())
        {
            CHECK(ev.frame == 0);
            CHECK(ev.rule_index < 2);
        }
        auto before = mem.of(Target::ROM);
        for (int i = 0; i < 30; i++)
            e.on_frame(mem);
        CHECK(mem.of(Target::ROM) == before); // only the live WRAM rule fires in frames
        CHECK(e.status().firings == 30 + 2);
    }

    TEST_CASE("a filter picks which targets fire, and the same seed gives the same bytes")
    {
        FakeMemory a, b;
        Engine e;
        e.set_recipe(classic());
        e.set_active(true);
        CHECK(e.fire_at_load(a, [](Target t) { return t == Target::ROM; }) == 8);
        CHECK(e.fire_at_load(a, [](Target t) { return t != Target::ROM; }) == 8);
        CHECK(a.of(Target::WRAM) != FakeMemory().of(Target::WRAM));

        e.set_active(false);
        e.set_active(true); // a new run
        e.fire_at_load(b);
        CHECK(a.of(Target::ROM) == b.of(Target::ROM));
        CHECK(a.of(Target::WRAM) == b.of(Target::WRAM));

        e.reroll(10);
        FakeMemory c;
        e.fire_at_load(c);
        CHECK(c.of(Target::ROM) != a.of(Target::ROM));
    }

    TEST_CASE("an inactive engine or a disabled rule fires nothing")
    {
        FakeMemory mem;
        Engine e;
        e.set_recipe(classic());
        CHECK(e.fire_at_load(mem) == 0);
        e.set_active(true);
        e.set_rule_enabled(0, false);
        CHECK(e.fire_at_load(mem) == 8);
    }
}

TEST_CASE("locked bytes are skipped by every rule, avoiding or not, and never logged")
{
    FakeMemory mem;
    Recipe r;
    r.seed = 3;
    Rule all;
    all.range = { Target::ROM, 0x100, 0x11F };
    all.op = { .type = OpType::Random, .min = 1, .max = 255 };
    all.when = { .type = TriggerType::EveryFrames, .every = 1 };
    all.count = 32;
    Rule avoiding = all;
    avoiding.avoid_hot = true;
    avoiding.range = { Target::ROM, 0x200, 0x21F };
    r.rules = { all, avoiding };

    HotMask locked;
    locked.add_block(Target::ROM, 0x100);
    locked.add_block(Target::ROM, 0x210);
    HotMask hot;
    hot.add_block(Target::ROM, 0x200);

    Engine e;
    e.set_recipe(r);
    e.set_locked(locked);
    e.set_hot_mask(hot);
    e.set_active(true);
    for (int i = 0; i < 20; i++)
        e.on_frame(mem);
    for (uint32_t a = 0x100; a < 0x110; a++)
        CHECK(mem.of(Target::ROM)[a] == 0);
    for (uint32_t a = 0x200; a < 0x220; a++) // hot (avoided) and locked
        CHECK(mem.of(Target::ROM)[a] == 0);
    CHECK(mem.of(Target::ROM)[0x115] != 0);
    for (auto &ev : e.event_log())
        CHECK_FALSE(locked.is_hot(Target::ROM, ev.write.offset));
}

TEST_SUITE("engine: looking ahead (#52)")
{
    Recipe live_rom()
    {
        Recipe r;
        r.name = "live rom";
        r.seed = 9;
        Rule drip;
        drip.name = "drip";
        drip.range = { Target::ROM, 0x100, 0x1FF };
        drip.op = { .type = OpType::Random, .min = 0, .max = 255 };
        drip.when = { .type = TriggerType::EveryFrames, .every = 3 };
        drip.count = 2;
        r.rules.push_back(drip);
        Rule noise; // WRAM, every frame: in the preview too, but not asked for
        noise.name = "noise";
        noise.range = { Target::WRAM, 0x0, 0xFF };
        noise.op = { .type = OpType::Random, .min = 0, .max = 255 };
        noise.when = { .type = TriggerType::EveryFrames, .every = 1 };
        r.rules.push_back(noise);
        return r;
    }

    TEST_CASE("preview_next tells the next frame's writes and changes nothing")
    {
        FakeMemory mem;
        Engine e;
        e.set_recipe(live_rom());
        e.set_active(true);
        frames(e, mem, 2);
        auto before = mem.bytes;
        size_t logged = e.event_log().size();
        auto runtimes = e.snapshot().runtimes;

        auto planned = e.preview_next(mem, Target::ROM);
        REQUIRE(planned.size() == 2);
        for (auto &p : planned)
        {
            CHECK(p.frame == 3);
            CHECK(p.write.target == Target::ROM);
        }
        CHECK(e.preview_next(mem).size() == 3); // the WRAM write too
        CHECK(mem.bytes == before);
        CHECK(e.current_frame() == 2);
        CHECK(e.event_log().size() == logged);
        CHECK(e.snapshot().runtimes.size() == runtimes.size());
        CHECK(e.snapshot().runtimes[0].firings == runtimes[0].firings);

        e.on_frame(mem); // the real frame makes exactly those writes
        std::vector<std::pair<uint32_t, uint8_t>> real, seen;
        for (auto &l : e.event_log())
            if (l.frame == 3 && l.write.target == Target::ROM)
                real.push_back({ l.write.offset, l.write.new_value });
        for (auto &p : planned)
            seen.push_back({ p.write.offset, p.write.new_value });
        CHECK(real == seen);
        CHECK(e.preview_next(mem, Target::ROM).empty()); // frame 4: the drip rests
    }

    TEST_CASE("a pressed hotkey survives a preview, and can be read and set back")
    {
        FakeMemory mem;
        Engine e;
        Recipe r;
        Rule key = hotkey_rule(2);
        key.range = { Target::ROM, 0x40, 0x40 };
        r.rules.push_back(key);
        e.set_recipe(r);
        e.set_active(true);
        e.press_hotkey(2);
        CHECK(e.pressed_hotkeys() != 0);
        CHECK(e.preview_next(mem, Target::ROM).size() == 1);
        CHECK(e.pressed_hotkeys() != 0);
        uint16_t bits = e.pressed_hotkeys();
        e.restore(e.snapshot()); // a restore forgets it...
        CHECK(e.pressed_hotkeys() == 0);
        e.set_pressed_hotkeys(bits); // ...a run ahead puts it back
        e.on_frame(mem);
        CHECK(mem.of(Target::ROM)[0x40] == 1);
    }

    TEST_CASE("vetoed writes are skipped and not logged in that frame only, through restores, until dropped")
    {
        FakeMemory mem;
        Engine e;
        e.set_recipe(live_rom());
        e.set_active(true);
        frames(e, mem, 2);
        auto snap = e.snapshot();
        auto planned = e.preview_next(mem, Target::ROM);
        REQUIRE(planned.size() == 2);
        e.veto(3, Target::ROM, { planned[0].write.offset });
        auto rom_before = mem.of(Target::ROM);

        e.on_frame(mem); // a run ahead's frame 3
        CHECK(mem.of(Target::ROM)[planned[0].write.offset] == rom_before[planned[0].write.offset]);
        CHECK(mem.of(Target::ROM)[planned[1].write.offset] == planned[1].write.new_value);
        size_t rom_logged = 0;
        for (auto &l : e.event_log())
            rom_logged += l.frame == 3 && l.write.target == Target::ROM;
        CHECK(rom_logged == 1);
        frames(e, mem, 3); // frame 6 drips both
        size_t at6 = 0;
        for (auto &l : e.event_log())
            at6 += l.frame == 6 && l.write.target == Target::ROM;
        CHECK(at6 == 2);

        e.restore(snap); // back before frame 3: still vetoed
        mem.of(Target::ROM) = rom_before;
        e.on_frame(mem);
        CHECK(mem.of(Target::ROM)[planned[0].write.offset] == rom_before[planned[0].write.offset]);
        CHECK(e.vetoes() == 1);
        e.drop_vetoes_through(3);
        CHECK(e.vetoes() == 0);
        e.veto(9, Target::ROM, { 1 });
        e.set_active(false);
        e.set_active(true); // a new run: the old run's vetoes mean nothing
        CHECK(e.vetoes() == 0);
    }
}

TEST_CASE("quiet_after keeps a target's writes from landing in later frames")
{
    FakeMemory mem;
    Engine e;
    Recipe r;
    Rule rom_every;
    rom_every.name = "rom";
    rom_every.range = { Target::ROM, 0x10, 0x10 };
    rom_every.op = { .type = OpType::Add, .min = 1, .max = 1 };
    rom_every.when = { .type = TriggerType::EveryFrames, .every = 1 };
    r.rules.push_back(rom_every);
    Rule wram = rom_every;
    wram.range = { Target::WRAM, 0x10, 0x10 };
    r.rules.push_back(wram);
    e.set_recipe(r);
    e.set_active(true);
    e.quiet_after(1);
    frames(e, mem, 3);
    CHECK(mem.of(Target::ROM)[0x10] == 1); // frame 1 only
    CHECK(mem.of(Target::WRAM)[0x10] == 3); // other targets as usual
    e.quiet_after(std::nullopt);
    e.on_frame(mem);
    CHECK(mem.of(Target::ROM)[0x10] == 2);
}
