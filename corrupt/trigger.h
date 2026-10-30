#pragma once
#include "rng.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace corrupt
{

// Frames are counted from activation: the first frame after turning a recipe on is frame 1.

enum class TriggerType
{
    OnceAtFrame,    // fire on `frame`
    EveryFrames,    // fire on every multiple of `every`
    ChancePerFrame, // fire with probability `chance` each frame
    Hotkey,         // fire on the frame hotkey `hotkey` (1-9) is pressed
};

struct FrameWindow
{
    uint64_t start = 0; // inclusive
    uint64_t end = 0;   // inclusive
};

struct Trigger
{
    TriggerType type = TriggerType::EveryFrames;
    uint64_t frame = 1;
    uint32_t every = 60;
    double chance = 0.0;
    int hotkey = 1;
    std::optional<FrameWindow> window;
};

constexpr uint16_t hotkey_bit(int hotkey) { return uint16_t(1u << hotkey); }

bool should_fire(const Trigger &t, uint64_t frame, Rng &rng, uint16_t pressed_hotkeys);
std::optional<std::string> validate_trigger(const Trigger &t);

const char *trigger_name(TriggerType t);
std::optional<TriggerType> parse_trigger(std::string_view name);

} // namespace corrupt
