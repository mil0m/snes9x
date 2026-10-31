#include "doctest/doctest.h"
#include "corrupt/trigger.h"

#include <vector>

using namespace corrupt;

static std::vector<uint64_t> firing_frames(const Trigger &t, uint64_t frames, uint16_t hotkeys_on_frame_10 = 0)
{
    std::vector<uint64_t> out;
    for (uint64_t f = 1; f <= frames; f++)
    {
        Rng rng = Rng::for_firing(1, 0, f);
        if (should_fire(t, f, rng, f == 10 ? hotkeys_on_frame_10 : 0))
            out.push_back(f);
    }
    return out;
}

TEST_CASE("once_at_frame fires exactly once")
{
    Trigger t{ .type = TriggerType::OnceAtFrame, .frame = 120 };
    CHECK(firing_frames(t, 300) == std::vector<uint64_t>{ 120 });
}

TEST_CASE("every_frames fires on multiples")
{
    Trigger t{ .type = TriggerType::EveryFrames, .every = 100 };
    CHECK(firing_frames(t, 300) == std::vector<uint64_t>{ 100, 200, 300 });

    Trigger each{ .type = TriggerType::EveryFrames, .every = 1 };
    CHECK(firing_frames(each, 300).size() == 300);
}

TEST_CASE("chance_per_frame at the extremes and in between")
{
    CHECK(firing_frames({ .type = TriggerType::ChancePerFrame, .chance = 0.0 }, 300).empty());
    CHECK(firing_frames({ .type = TriggerType::ChancePerFrame, .chance = 1.0 }, 300).size() == 300);
    auto some = firing_frames({ .type = TriggerType::ChancePerFrame, .chance = 0.1 }, 3000).size();
    CHECK(some > 200);
    CHECK(some < 400);
}

TEST_CASE("hotkey fires only on the frame its key was pressed")
{
    Trigger t{ .type = TriggerType::Hotkey, .hotkey = 3 };
    CHECK(firing_frames(t, 300, hotkey_bit(3)) == std::vector<uint64_t>{ 10 });
    CHECK(firing_frames(t, 300, hotkey_bit(4)).empty());
}

TEST_CASE("window limits firing, inclusive at both ends")
{
    Trigger t{ .type = TriggerType::EveryFrames, .every = 1, .window = FrameWindow{ 50, 59 } };
    auto frames = firing_frames(t, 300);
    REQUIRE(frames.size() == 10);
    CHECK(frames.front() == 50);
    CHECK(frames.back() == 59);
}

TEST_CASE("window applies to hotkeys too")
{
    Trigger t{ .type = TriggerType::Hotkey, .hotkey = 1, .window = FrameWindow{ 100, 200 } };
    CHECK(firing_frames(t, 300, hotkey_bit(1)).empty()); // pressed on frame 10
}

TEST_CASE("at_load fires only at frame 0, before the run's first frame")
{
    Trigger t{ .type = TriggerType::AtLoad };
    CHECK(firing_frames(t, 300).empty());
    Rng rng = Rng::for_firing(1, 0, 0);
    CHECK(should_fire(t, 0, rng, 0));
    CHECK_FALSE(validate_trigger(t));
    CHECK(parse_trigger("at_load") == TriggerType::AtLoad);
    CHECK(std::string(trigger_name(TriggerType::AtLoad)) == "at_load");
}

TEST_CASE("trigger validation")
{
    CHECK_FALSE(validate_trigger({ .type = TriggerType::EveryFrames, .every = 30 }));
    CHECK(validate_trigger({ .type = TriggerType::EveryFrames, .every = 0 }));
    CHECK(validate_trigger({ .type = TriggerType::ChancePerFrame, .chance = 1.5 }));
    CHECK(validate_trigger({ .type = TriggerType::Hotkey, .hotkey = 0 }));
    CHECK(validate_trigger({ .type = TriggerType::Hotkey, .hotkey = 10 }));
    CHECK(validate_trigger({ .type = TriggerType::OnceAtFrame, .frame = 0 }));
    CHECK(validate_trigger({ .type = TriggerType::EveryFrames, .every = 1, .window = FrameWindow{ 9, 3 } }));
}
