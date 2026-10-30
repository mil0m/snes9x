#include "doctest/doctest.h"
#include "corrupt/recipe_json.h"

using namespace corrupt;

namespace
{
const char *kExample = R"({
  "format": 1,
  "name": "Purple Link chaos",
  "game": { "title": "ZELDA", "sha256": "ab12", "crc32": "1234abcd" },
  "seed": 1337,
  "notes": "Palette drift + slightly drunk enemies.",
  "anchor_state": "purple-link-chaos.anchor.frz",
  "rules": [
    {
      "name": "palette drift",
      "target": "cgram", "range": ["0x000", "0x1FF"],
      "op": { "type": "add", "min": -2, "max": 2 },
      "when": { "every_frames": 30 },
      "count": 4
    },
    {
      "name": "enemy chaos",
      "target": "wram", "range": "enemy_table",
      "op": { "type": "random", "min": "0x00", "max": "0x3F" },
      "when": { "chance_per_frame": 0.02 },
      "count": 1
    },
    {
      "name": "swap two stats",
      "target": "wram",
      "op": { "type": "swap", "a": "0x0F00", "b": "0x0F02", "width": 2 },
      "when": { "hotkey": 1 }
    }
  ]
})";

Regions example_regions()
{
    Regions r;
    r["enemy_table"] = { Target::WRAM, 0x0D00, 0x0DFF };
    return r;
}

std::string first_error(const ParseResult &r)
{
    return r.errors.empty() ? "" : r.errors.front();
}
} // namespace

TEST_CASE("parses the spec example")
{
    auto r = parse_recipe(kExample, example_regions());
    REQUIRE_MESSAGE(r.recipe.has_value(), first_error(r));
    auto &rec = *r.recipe;
    CHECK(rec.name == "Purple Link chaos");
    CHECK(rec.game.title == "ZELDA");
    CHECK(rec.seed == 1337);
    CHECK(rec.anchor_state == "purple-link-chaos.anchor.frz");
    REQUIRE(rec.rules.size() == 3);

    auto &drift = rec.rules[0];
    CHECK(drift.range.target == Target::CGRAM);
    CHECK(drift.range.last == 0x1FF);
    CHECK(drift.op.type == OpType::Add);
    CHECK(drift.op.min == -2);
    CHECK(drift.when.type == TriggerType::EveryFrames);
    CHECK(drift.when.every == 30);
    CHECK(drift.count == 4);

    auto &chaos = rec.rules[1];
    CHECK(chaos.region == "enemy_table");
    CHECK(chaos.range.first == 0x0D00);
    CHECK(chaos.op.max == 0x3F);
    CHECK(chaos.when.chance == doctest::Approx(0.02));

    auto &swap = rec.rules[2];
    CHECK(swap.range.first == 0);
    CHECK(swap.range.last == 0x1FFFF); // no range: whole target
    CHECK(swap.op.a == 0x0F00u);
    CHECK(swap.op.width == 2);
    CHECK(swap.when.hotkey == 1);
}

TEST_CASE("defaults")
{
    auto r = parse_recipe(R"({"format":1,"name":"x","seed":1,"rules":[
        {"target":"wram","range":["0x10","0x20"],"op":{"type":"bitflip"},"when":{"every_frames":5}}]})", {});
    REQUIRE_MESSAGE(r.recipe, first_error(r));
    auto &rule = r.recipe->rules[0];
    CHECK(rule.name == "rule 1");
    CHECK(rule.count == 1);
    CHECK(rule.pick == Pick::Fresh);
    CHECK(rule.enabled);
    CHECK(rule.intensity == 1.0);
    CHECK(rule.op.bits == 1);
}

TEST_CASE("writing is canonical and idempotent")
{
    auto first = parse_recipe(kExample, example_regions());
    REQUIRE(first.recipe);
    auto text = write_recipe(*first.recipe);
    auto second = parse_recipe(text, example_regions());
    REQUIRE_MESSAGE(second.recipe, first_error(second));
    CHECK(write_recipe(*second.recipe) == text);

    CHECK(text.find("\"range\": \"enemy_table\"") != std::string::npos); // regions stay named
    CHECK(text.find("\"0x0F00\"") != std::string::npos);                 // addresses in hex
}

TEST_CASE("every field survives a round trip")
{
    auto r = parse_recipe(R"({"format":1,"name":"all","seed":18446744073709551615,"notes":"n","rules":[
        {"name":"c","target":"wram","range":["0x100","0x1FF"],
         "op":{"type":"copy","from":{"target":"vram","range":["0x0","0xFF"]},"length":16},
         "when":{"chance_per_frame":0.5,"window":[10,20]},
         "count":3,"pick":"sticky","enabled":false,"intensity":0.25,"avoid_hot":true},
        {"target":"oam","range":["0","0x21F"],"op":{"type":"freeze","value":"0x63"},"when":{"once_at_frame":9}},
        {"target":"aram","range":["0","0xFFFF"],"op":{"type":"bitflip","mask":"0x81"},"when":{"every_frames":2}},
        {"target":"wram","range":["0","0xFF"],"op":{"type":"add","min":-5,"max":5,"mode":"clamp"},"when":{"every_frames":2}},
        {"target":"wram","range":["0","0xFF"],"op":{"type":"shuffle","window":4},"when":{"every_frames":2}}]})", {});
    REQUIRE_MESSAGE(r.recipe, first_error(r));
    CHECK(r.recipe->seed == 18446744073709551615ULL);
    auto &c = r.recipe->rules[0];
    CHECK(c.op.from->target == Target::VRAM);
    CHECK(c.op.length == 16);
    CHECK(c.when.window->end == 20);
    CHECK(c.pick == Pick::Sticky);
    CHECK_FALSE(c.enabled);
    CHECK(c.avoid_hot);
    CHECK(r.recipe->rules[1].op.value == uint8_t(0x63));
    CHECK(r.recipe->rules[2].op.mask == uint8_t(0x81));
    CHECK(r.recipe->rules[3].op.clamp);
    CHECK(r.recipe->rules[4].op.window == 4);

    auto again = parse_recipe(write_recipe(*r.recipe), {});
    REQUIRE_MESSAGE(again.recipe, first_error(again));
    CHECK(write_recipe(*again.recipe) == write_recipe(*r.recipe));
}

TEST_CASE("rule salt round-trips and is omitted when zero")
{
    auto r = parse_recipe(R"({"format":1,"name":"s","rules":[
        {"target":"wram","range":["0","9"],"op":{"type":"random"},"when":{"hotkey":1},"salt":987654321},
        {"target":"wram","range":["0","9"],"op":{"type":"random"},"when":{"hotkey":1}}]})", {});
    REQUIRE_MESSAGE(r.recipe, first_error(r));
    CHECK(r.recipe->rules[0].salt == 987654321u);
    CHECK(r.recipe->rules[1].salt == 0u);
    auto text = write_recipe(*r.recipe);
    CHECK(text.find("987654321") != std::string::npos);
    CHECK(text.find("\"salt\"") == text.rfind("\"salt\"")); // only once: zero is left out
}

TEST_CASE("crash policy and detection settings")
{
    auto r = parse_recipe(R"({"format":1,"name":"p","crash_policy":"escalate",
        "crash_detection":{"frozen_seconds":0,"nmi_off_seconds":5,"black_seconds":2.5,"runaway_frames":10,"cpu_stopped":false},
        "rules":[]})", {});
    REQUIRE_MESSAGE(r.recipe, first_error(r));
    REQUIRE(r.recipe->crash_policy);
    CHECK(*r.recipe->crash_policy == PolicyKind::Escalate);
    REQUIRE(r.recipe->crash_detection);
    auto &d = *r.recipe->crash_detection;
    CHECK(d.frozen_frames == 0);
    CHECK(d.nmi_off_frames == 300);
    CHECK(d.black_frames == 150);
    CHECK(d.runaway_frames == 10);
    CHECK_FALSE(d.cpu_stopped);
    CHECK(d.enabled);

    auto text = write_recipe(*r.recipe);
    auto again = parse_recipe(text, {});
    REQUIRE_MESSAGE(again.recipe, first_error(again));
    CHECK(write_recipe(*again.recipe) == text);
    CHECK(again.recipe->crash_detection->black_frames == 150);
}

TEST_CASE("unset crash settings stay unset and aren't written")
{
    auto r = parse_recipe(R"({"format":1,"name":"p","rules":[]})", {});
    REQUIRE(r.recipe);
    CHECK_FALSE(r.recipe->crash_policy);
    CHECK_FALSE(r.recipe->crash_detection);
    CHECK(write_recipe(*r.recipe).find("crash") == std::string::npos);
}

TEST_CASE("partial detection settings keep the defaults for the rest")
{
    auto r = parse_recipe(R"({"format":1,"rules":[],"crash_detection":{"enabled":false}})", {});
    REQUIRE(r.recipe);
    CHECK_FALSE(r.recipe->crash_detection->enabled);
    CHECK(r.recipe->crash_detection->frozen_frames == DetectorConfig{}.frozen_frames);
}

TEST_CASE("bad crash settings are errors")
{
    CHECK_FALSE(parse_recipe(R"({"format":1,"rules":[],"crash_policy":"yolo"})", {}).recipe);
    CHECK_FALSE(parse_recipe(R"({"format":1,"rules":[],"crash_detection":{"frozen_secs":3}})", {}).recipe);
    CHECK_FALSE(parse_recipe(R"({"format":1,"rules":[],"crash_detection":{"frozen_seconds":-1}})", {}).recipe);
}

TEST_SUITE("errors")
{
    TEST_CASE("broken JSON")
    {
        auto r = parse_recipe("{\"format\": 1,", {});
        CHECK_FALSE(r.recipe);
        CHECK(first_error(r).find("JSON") != std::string::npos);
    }

    TEST_CASE("unknown format version")
    {
        auto r = parse_recipe(R"({"format":2,"name":"x","rules":[]})", {});
        CHECK_FALSE(r.recipe);
        CHECK(first_error(r).find("format") != std::string::npos);
    }

    TEST_CASE("unknown region names the region")
    {
        auto r = parse_recipe(R"({"format":1,"rules":[{"target":"wram","range":"boss_hp","op":{"type":"random","max":9},"when":{"every_frames":1}}]})", {});
        CHECK_FALSE(r.recipe);
        CHECK(first_error(r).find("boss_hp") != std::string::npos);
        CHECK(first_error(r).find("rules[0]") != std::string::npos);
    }

    TEST_CASE("bad op values carry the rule path")
    {
        auto r = parse_recipe(R"({"format":1,"rules":[
            {"target":"wram","range":["0","9"],"op":{"type":"random","max":1},"when":{"every_frames":1}},
            {"target":"wram","range":["0","9"],"op":{"type":"random","min":0,"max":300},"when":{"every_frames":1}}]})", {});
        CHECK_FALSE(r.recipe);
        CHECK(first_error(r).find("rules[1].op") != std::string::npos);
        CHECK(first_error(r).find("max") != std::string::npos);
    }

    TEST_CASE("typos in keys are caught")
    {
        auto r = parse_recipe(R"({"format":1,"rules":[{"target":"wram","range":["0","9"],"op":{"type":"random","max":1},"when":{"evry_frames":1}}]})", {});
        CHECK_FALSE(r.recipe);
        CHECK(first_error(r).find("evry_frames") != std::string::npos);
    }

    TEST_CASE("exactly one trigger")
    {
        auto none = parse_recipe(R"({"format":1,"rules":[{"target":"wram","range":["0","9"],"op":{"type":"random","max":1},"when":{}}]})", {});
        auto two = parse_recipe(R"({"format":1,"rules":[{"target":"wram","range":["0","9"],"op":{"type":"random","max":1},"when":{"hotkey":1,"every_frames":3}}]})", {});
        CHECK_FALSE(none.recipe);
        CHECK_FALSE(two.recipe);
    }

    TEST_CASE("bad target and op names")
    {
        CHECK_FALSE(parse_recipe(R"({"format":1,"rules":[{"target":"rom","range":["0","9"],"op":{"type":"random"},"when":{"hotkey":1}}]})", {}).recipe);
        CHECK_FALSE(parse_recipe(R"({"format":1,"rules":[{"target":"wram","range":["0","9"],"op":{"type":"explode"},"when":{"hotkey":1}}]})", {}).recipe);
    }

    TEST_CASE("ranges past the end of a target")
    {
        auto r = parse_recipe(R"({"format":1,"rules":[{"target":"cgram","range":["0","0x200"],"op":{"type":"random"},"when":{"hotkey":1}}]})", {});
        CHECK_FALSE(r.recipe);
        CHECK(first_error(r).find("rules[0].range") != std::string::npos);
    }

    TEST_CASE("sram needs an explicit range")
    {
        auto r = parse_recipe(R"({"format":1,"rules":[{"target":"sram","op":{"type":"random"},"when":{"hotkey":1}}]})", {});
        CHECK_FALSE(r.recipe);
        CHECK(first_error(r).find("sram") != std::string::npos);
    }

    TEST_CASE("all errors are reported, not just the first")
    {
        auto r = parse_recipe(R"({"format":1,"rules":[
            {"target":"rom","op":{"type":"random"},"when":{"hotkey":1}},
            {"target":"wram","range":["0","9"],"op":{"type":"explode"},"when":{"hotkey":1}}]})", {});
        CHECK(r.errors.size() == 2);
    }
}

TEST_SUITE("numbers")
{
    TEST_CASE("hex strings, decimal strings and plain numbers")
    {
        auto r = parse_recipe(R"({"format":1,"rules":[{"target":"wram","range":["0x10", "32"],"op":{"type":"add","min":"-3","max":"0x3"},"when":{"every_frames":"0x1E"}}]})", {});
        REQUIRE_MESSAGE(r.recipe, first_error(r));
        auto &rule = r.recipe->rules[0];
        CHECK(rule.range.first == 0x10);
        CHECK(rule.range.last == 32);
        CHECK(rule.op.min == -3);
        CHECK(rule.op.max == 3);
        CHECK(rule.when.every == 30);
    }

    TEST_CASE("garbage numbers are errors")
    {
        CHECK_FALSE(parse_recipe(R"({"format":1,"rules":[{"target":"wram","range":["0xZZ","9"],"op":{"type":"random"},"when":{"hotkey":1}}]})", {}).recipe);
    }
}

TEST_SUITE("regions")
{
    TEST_CASE("parse regions.json")
    {
        auto r = parse_regions(R"({"format":1,"regions":{
            "player_stats":{"target":"wram","range":["0x0F00","0x0F3F"]},
            "sprites":{"target":"oam","range":["0","0x1FF"]}}})");
        REQUIRE(r.errors.empty());
        CHECK(r.regions.size() == 2);
        CHECK(r.regions.at("player_stats").last == 0x0F3F);
        CHECK(r.regions.at("sprites").target == Target::OAM);
    }

    TEST_CASE("bad region entries are reported and skipped")
    {
        auto r = parse_regions(R"({"format":1,"regions":{"ok":{"target":"wram","range":["0","1"]},"bad":{"target":"rom","range":["0","1"]}}})");
        CHECK(r.errors.size() == 1);
        CHECK(r.regions.count("ok") == 1);
    }

    TEST_CASE("regions round-trip")
    {
        Regions in;
        in["a"] = { Target::VRAM, 0x100, 0x1FF };
        auto out = parse_regions(write_regions(in));
        REQUIRE(out.errors.empty());
        CHECK(out.regions.at("a").first == 0x100);
    }
}

TEST_CASE("numbers typed in the panel parse like the ones in recipes")
{
    CHECK(parse_number("0x1F") == 0x1F);
    CHECK(parse_number("0X1f") == 0x1F);
    CHECK(parse_number("$7E") == 0x7E);
    CHECK(parse_number("42") == 42);
    CHECK(parse_number("-3") == -3);
    CHECK(parse_number("  0x2000 ") == 0x2000);
    CHECK_FALSE(parse_number(""));
    CHECK_FALSE(parse_number("   "));
    CHECK_FALSE(parse_number("0x"));
    CHECK_FALSE(parse_number("$"));
    CHECK_FALSE(parse_number("12a"));
    CHECK_FALSE(parse_number("0x12g"));
    CHECK_FALSE(parse_number("1 2"));
    CHECK_FALSE(parse_number("0x10000000000000000")); // overflows 64 bits
    CHECK_FALSE(parse_number("99999999999999999999"));
}

TEST_CASE("recipe intensity round-trips, and is left out at full strength")
{
    auto parsed = parse_recipe(R"({"format":1,"name":"x","intensity":0.4,"rules":[]})", {});
    REQUIRE(parsed.recipe);
    CHECK(parsed.recipe->intensity == 0.4);
    auto text = write_recipe(*parsed.recipe);
    CHECK(text.find("\"intensity\": 0.4") != std::string::npos);
    CHECK(parse_recipe(text, {}).recipe->intensity == 0.4);

    parsed.recipe->intensity = 1.0;
    CHECK(write_recipe(*parsed.recipe).find("intensity") == std::string::npos);
    CHECK(parse_recipe(R"({"format":1,"name":"x","rules":[]})", {}).recipe->intensity == 1.0);
}

TEST_CASE("recipe intensity must be a number within 0..1")
{
    auto high = parse_recipe(R"({"format":1,"name":"x","intensity":1.5,"rules":[]})", {});
    REQUIRE_FALSE(high.recipe);
    CHECK(high.errors[0].find("recipe.intensity") != std::string::npos);
    CHECK_FALSE(parse_recipe(R"({"format":1,"name":"x","intensity":-0.1,"rules":[]})", {}).recipe);
    CHECK_FALSE(parse_recipe(R"({"format":1,"name":"x","intensity":"lots","rules":[]})", {}).recipe);
}

TEST_CASE("reboot detection can be switched off per recipe")
{
    auto parsed = parse_recipe(R"({"format":1,"name":"x","crash_detection":{"reboot":false},"rules":[]})", {});
    REQUIRE(parsed.recipe);
    REQUIRE(parsed.recipe->crash_detection);
    CHECK_FALSE(parsed.recipe->crash_detection->reboot);
    auto again = parse_recipe(write_recipe(*parsed.recipe), {});
    REQUIRE(again.recipe);
    CHECK_FALSE(again.recipe->crash_detection->reboot);
    CHECK(DetectorConfig{}.reboot);
    CHECK_FALSE(parse_recipe(R"({"format":1,"name":"x","crash_detection":{"reboot":"no"},"rules":[]})", {}).recipe);
}
