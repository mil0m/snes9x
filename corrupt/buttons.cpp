#include "buttons.h"

#include <array>

namespace corrupt
{

namespace
{

std::string function_label(const ButtonState &state, uint32_t entry)
{
    std::string name = state.function_name ? state.function_name(entry) : std::string();
    return name.empty() ? hex_address(entry, Target::ROM) : name;
}

} // namespace

std::vector<ButtonSlot> recipe_buttons(const Recipe &recipe, const ButtonState &state)
{
    struct Slot
    {
        std::vector<std::string> names;
        bool has_rule = false;
        bool all_on = true;
    };
    std::array<Slot, 10> slots;
    auto in_range = [](int key) { return key >= 1 && key <= 9; };

    for (auto &rule : recipe.rules)
        if (rule.enabled && rule.when.type == TriggerType::Hotkey && in_range(rule.when.hotkey))
        {
            auto &s = slots[size_t(rule.when.hotkey)];
            s.names.push_back(rule.name);
            s.has_rule = true;
        }
    for (auto &sw : recipe.switches)
        if (in_range(sw.hotkey))
        {
            auto &s = slots[size_t(sw.hotkey)];
            s.names.push_back(!sw.note.empty() ? sw.note : function_label(state, sw.entry) + " off");
            if (!(state.switch_off && state.switch_off(sw)))
                s.all_on = false;
        }
    for (auto &c : recipe.changes)
        if (in_range(c.hotkey))
        {
            auto &s = slots[size_t(c.hotkey)];
            s.names.push_back(!c.note.empty() ? c.note : function_label(state, c.function) + " changed");
            if (!(state.change_on && state.change_on(c)))
                s.all_on = false;
        }

    std::vector<ButtonSlot> out;
    for (int key = 1; key <= 9; key++)
    {
        auto &s = slots[size_t(key)];
        if (s.names.empty())
            continue;
        ButtonSlot b;
        b.key = key;
        b.label = s.names[0];
        if (s.names.size() > 1)
            b.label += " + " + std::to_string(s.names.size() - 1) + " more";
        for (size_t i = 0; i < s.names.size(); i++)
            b.details += (i ? "\n" : "") + s.names[i];
        b.toggle = !s.has_rule;
        b.on = b.toggle && s.all_on;
        out.push_back(b);
    }
    return out;
}

} // namespace corrupt

namespace corrupt
{

namespace
{

std::string seconds(uint32_t frames)
{
    char text[32];
    if (frames % 60 == 0)
        snprintf(text, sizeof text, "%u s", frames / 60);
    else
        snprintf(text, sizeof text, "%.1f s", frames / 60.0);
    return text;
}

std::string count(size_t n, const char *one, const char *many)
{
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

const char *change_words(ChangeKind k)
{
    switch (k)
    {
    case ChangeKind::Number: return "a number";
    case ChangeKind::Branch: return "a branch";
    case ChangeKind::Math: return "a math step";
    case ChangeKind::Skip: return "a skipped call";
    case ChangeKind::Redirect: return "a call sent elsewhere";
    }
    return "a change";
}

std::string join(const std::vector<std::string> &parts)
{
    std::string out;
    for (auto &p : parts)
        out += (out.empty() ? "" : " · ") + p;
    return out;
}

} // namespace

std::vector<KeptItem> recipe_kept(const Recipe &recipe, const ButtonState &state)
{
    std::vector<KeptItem> out;
    for (auto &sw : recipe.switches)
    {
        KeptItem k;
        k.kind = KeptItem::Kind::Switch;
        k.at = sw.entry;
        k.what = !sw.note.empty() ? sw.note : function_label(state, sw.entry) + " off";
        std::vector<std::string> parts;
        if (sw.hotkey)
            parts.push_back("button " + std::to_string(sw.hotkey));
        if (sw.start_off)
            parts.push_back("starts off");
        if (sw.every && sw.for_frames)
            parts.push_back("off " + seconds(sw.for_frames) + " of every " + seconds(sw.every));
        k.details = join(parts);
        out.push_back(k);
    }
    for (auto &c : recipe.changes)
    {
        KeptItem k;
        k.kind = KeptItem::Kind::Change;
        k.at = c.at;
        k.what = !c.note.empty() ? c.note : function_label(state, c.function) + " changed";
        std::vector<std::string> parts{ std::string(change_words(c.kind)) + " in " + function_label(state, c.function) };
        if (c.hotkey)
            parts.push_back("button " + std::to_string(c.hotkey));
        parts.push_back(c.start_on ? "starts changed" : "starts as it was");
        if (c.every && c.for_frames)
            parts.push_back("on " + seconds(c.for_frames) + " of every " + seconds(c.every));
        k.details = join(parts);
        out.push_back(k);
    }
    for (auto &p : recipe.protects)
    {
        KeptItem k;
        k.kind = KeptItem::Kind::Protect;
        k.at = p.entry;
        k.what = function_label(state, p.entry) + " protected";
        k.details = p.deep ? "and what it calls" : "";
        out.push_back(k);
    }
    if (!recipe.pins.empty())
    {
        KeptItem k;
        k.kind = KeptItem::Kind::Pins;
        k.what = count(recipe.pins.size(), "pinned byte", "pinned bytes");
        k.details = "ROM changes kept from the survivors";
        out.push_back(k);
    }
    if (size_t n = patched_bytes(recipe.patches))
    {
        KeptItem k;
        k.kind = KeptItem::Kind::Paint;
        k.what = count(n, "painted byte", "painted bytes");
        k.details = "set as every run starts";
        out.push_back(k);
    }
    return out;
}

} // namespace corrupt
