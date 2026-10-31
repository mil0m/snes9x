#include "doctest/doctest.h"
#include "corrupt/recipe_json.h"
#include "corrupt/share.h"

using namespace corrupt;

TEST_SUITE("sharing")
{
    Recipe sample()
    {
        Recipe r;
        r.name = "Purple Link";
        r.notes = "everything turns purple";
        r.seed = 77;
        r.game = { "ZELDA3", "abc123" };
        Rule rule;
        rule.name = "tint";
        rule.range = { Target::WRAM, 0x0100, 0x01FF };
        rule.region = "palette_shadow";
        rule.op = { .type = OpType::Add, .min = -1, .max = 1 };
        rule.when = { .type = TriggerType::EveryFrames, .every = 30 };
        r.rules = { rule };
        r.pins = { { 0x1234, 0xEA, 0x20, "quiet" } };
        r.anchor_state = "purple-link.anchor.frz";
        return r;
    }

    TEST_CASE("a shared recipe carries its moment and pins, and needs nothing from the sender's library")
    {
        std::vector<uint8_t> moment(70000);
        for (size_t i = 0; i < moment.size(); i++)
            moment[i] = uint8_t(i * 31 + 7);
        auto text = write_shared(sample(), &moment);
        CHECK(text.find("palette_shadow") == std::string::npos); // named areas become plain ranges

        std::string error;
        auto back = read_shared(text, error);
        REQUIRE_MESSAGE(back, error);
        CHECK(back->recipe.name == "Purple Link");
        CHECK(back->recipe.notes == "everything turns purple");
        CHECK(back->recipe.seed == 77);
        REQUIRE(back->recipe.rules.size() == 1);
        CHECK(back->recipe.rules[0].range.first == 0x0100);
        CHECK(back->recipe.rules[0].region.empty());
        CHECK(back->recipe.pins.size() == 1);
        CHECK(back->recipe.game.sha256 == "abc123");
        CHECK(back->recipe.anchor_state.empty()); // the receiver's own file name comes later
        REQUIRE(back->moment);
        CHECK(*back->moment == moment);
    }

    TEST_CASE("without a moment, and odd lengths of base64")
    {
        for (size_t n : { 0, 1, 2, 3, 4, 5 })
        {
            std::vector<uint8_t> bytes(n, 0xAB);
            std::string error;
            auto back = read_shared(write_shared(sample(), n ? &bytes : nullptr), error);
            REQUIRE_MESSAGE(back, error);
            CHECK(back->moment.has_value() == (n > 0));
            if (n)
                CHECK(*back->moment == bytes);
        }
    }

    TEST_CASE("anything else is refused with a reason")
    {
        std::string error;
        CHECK(!read_shared("", error));
        CHECK(!error.empty());
        CHECK(!read_shared(R"({"format":"something else"})", error));
        CHECK(!read_shared(R"({"format":"snes9x-shared-recipe","version":1,"recipe":{"format":1,"rules":[{}]}})", error));
        CHECK(!read_shared(R"({"format":"snes9x-shared-recipe","version":1,"recipe":{"format":1,"rules":[]},"moment":"!!"})", error));
    }
}
