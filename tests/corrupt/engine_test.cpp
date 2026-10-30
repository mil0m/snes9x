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
