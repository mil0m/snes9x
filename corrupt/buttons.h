#pragma once
#include "recipe.h"

#include <functional>
#include <string>
#include <vector>

namespace corrupt
{

// The recipe's buttons (#63): slots 1-9 that fire rules and flip kept switches and changes.
// (The recipe calls the slot `hotkey`, its old name; the engine presses it with press_hotkey.)
// One button per slot in use, named after what it does.
struct ButtonSlot
{
    int key = 0;          // 1-9
    std::string label;    // "gravity off", "Burst", "Burst + 2 more"
    std::string details;  // everything on it, one per line
    bool toggle = false;  // only switches and changes: it stays on until pressed again
    bool on = false;      // a toggle that's on (its switches off, its changes in)
};

struct ButtonState
{
    // A function's name for the label ("gravity"); empty when it has none.
    std::function<std::string(uint32_t entry)> function_name;
    std::function<bool(const Switch &)> switch_off;
    std::function<bool(const Change &)> change_on;
};

// Slots in key order; a slot nothing uses isn't listed.
std::vector<ButtonSlot> recipe_buttons(const Recipe &recipe, const ButtonState &state);

// What a recipe keeps besides its rules, one line each, for the Workshop (#63): its
// switches, changes and protects, and how many pins and painted bytes.
struct KeptItem
{
    enum class Kind : uint8_t
    {
        Switch,
        Change,
        Protect,
        Pins,
        Paint,
    };
    Kind kind = Kind::Switch;
    uint32_t at = 0;     // a switch's or protect's entry, a change's address; 0 for pins and paint
    std::string what;    // "gravity off", "12 pinned bytes"
    std::string details; // "button 2 · starts off · off 1 s of every 10 s"
};
std::vector<KeptItem> recipe_kept(const Recipe &recipe, const ButtonState &state);

} // namespace corrupt
