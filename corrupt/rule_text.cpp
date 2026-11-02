#include "rule_text.h"

#include <cstdio>

namespace corrupt
{

namespace
{

std::string hex2(uint8_t v)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "0x%02X", v);
    return buf;
}

std::string plural(long n, const char *word)
{
    return std::to_string(n) + " " + word + (n == 1 ? "" : "s");
}

} // namespace

std::string describe_op(const Rule &rule)
{
    const Op &op = rule.op;
    std::string text;
    switch (op.type)
    {
    case OpType::Add:
        text = "add " + std::to_string(op.min) + ".." + std::to_string(op.max) + (op.clamp ? " clamped" : "");
        break;
    case OpType::Random:
        text = "random " + std::to_string(op.min) + ".." + std::to_string(op.max);
        break;
    case OpType::Bitflip:
        text = op.mask ? "xor " + hex2(*op.mask) : "flip " + plural(op.bits, "bit");
        break;
    case OpType::Swap:
        if (op.a && op.b) // a fixed pair: count doesn't apply
            return "swap " + hex_address(*op.a, rule.range.target) + " <-> " + hex_address(*op.b, rule.range.target) + (op.width == 2 ? ", 16-bit" : "");
        text = "swap with random";
        break;
    case OpType::Copy:
        text = "copy " + plural(long(op.length), "byte") + " from " +
               target_name(op.from ? op.from->target : rule.range.target);
        break;
    case OpType::Freeze:
        text = op.value ? "freeze at " + hex2(*op.value) : "freeze as hit";
        break;
    case OpType::Shuffle:
        text = "shuffle " + plural(op.window, "byte");
        break;
    case OpType::Shift:
        return "shift " + std::to_string(op.dx) + ", " + std::to_string(op.dy) + (op.wrap ? "" : ", no wrap");
    case OpType::Flip:
        return op.axis == Axis::Both ? "flip both ways" : std::string("flip ") + axis_name(op.axis);
    case OpType::Levels:
    {
        if (op.curve == Curve::Invert)
            return "invert";
        char buf[48];
        snprintf(buf, sizeof(buf), "%s %g", curve_name(op.curve), op.amount);
        return buf;
    }
    case OpType::Filter:
        return std::string(kernel_name(op.kernel)) + " filter";
    case OpType::Stamp:
        return "stamp from " + hex_address(op.source, rule.range.target);
    case OpType::Fill:
        return "fill " + hex2(op.value.value_or(0));
    case OpType::OpcodeSwap: text = "swap opcodes"; break;
    case OpType::BranchFlip: text = "flip branches"; break;
    case OpType::NopOut: text = "NOP out"; break;
    case OpType::PointerSwap:
        text = std::string("swap pointers") + (op.mutual ? " both ways" : "") + (op.likely ? " (unused entries too)" : "");
        break;
    case OpType::Kill:
        text = op.any ? "turn functions off (any)" : "turn busy functions off";
        if (op.put_back)
        {
            char back[48];
            std::snprintf(back, sizeof back, ", back on after %g s", op.put_back / 60.0);
            text += back;
        }
        break;
    }
    return text + " x" + std::to_string(rule.count);
}

std::string describe_trigger(const Trigger &t)
{
    std::string text;
    switch (t.type)
    {
    case TriggerType::OnceAtFrame: text = "once at frame " + std::to_string(t.frame); break;
    case TriggerType::EveryFrames: text = t.every == 1 ? "every frame" : "every " + std::to_string(t.every) + " frames"; break;
    case TriggerType::ChancePerFrame:
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.3g%% per frame", t.chance * 100.0);
        text = buf;
        break;
    }
    case TriggerType::Hotkey: text = "hotkey " + std::to_string(t.hotkey); break;
    case TriggerType::AtLoad: text = "once at load"; break;
    }
    if (t.window)
        text += ", frames " + std::to_string(t.window->start) + "-" + std::to_string(t.window->end);
    return text;
}

std::string describe_range(const Rule &rule)
{
    std::string where = target_name(rule.range.target);
    if (rule.rect)
        return where + " " + std::to_string(rule.rect->width) + "x" + std::to_string(rule.rect->height) + " at " +
               hex_address(rule.range.first, rule.range.target) + " (rows of " + std::to_string(rule.rect->row) + ")";
    if (!rule.region.empty())
        return where + " " + rule.region;
    return where + " " + hex_address(rule.range.first, rule.range.target) + ".." +
           hex_address(rule.range.last, rule.range.target);
}

std::string describe_aim(const Aim &aim)
{
    auto names = [](const std::vector<OpClass> &list) {
        std::string out;
        for (auto c : list)
            out += (out.empty() ? "" : ", ") + std::string(class_name(c));
        return out;
    };
    std::string role;
    switch (aim.role)
    {
    case Role::Any: break;
    case Role::Opcode: role = "opcodes"; break;
    case Role::Operand: role = "operands"; break;
    case Role::Code: role = "code"; break;
    case Role::Data: role = "data"; break;
    case Role::Dma: role = "DMA bytes"; break;
    case Role::Used: role = "used bytes"; break;
    }
    std::string text = role;
    if (!aim.classes.empty())
        text = role.empty() ? "only " + names(aim.classes) : role + " of " + names(aim.classes);
    if (!aim.except.empty())
        text = (text.empty() ? std::string("code") : text) + " except " + names(aim.except);
    if (aim.in_busy_functions)
        text = (text.empty() ? std::string("bytes") : text) + " inside busy functions";
    return text;
}

std::string describe_rule(const Rule &rule)
{
    std::string aim = describe_aim(rule.aim);
    return describe_range(rule) + ": " + describe_op(rule) + ", " + describe_trigger(rule.when) +
           (aim.empty() ? "" : ", " + aim) + (rule.avoid_hot ? ", avoiding danger" : "");
}

Rule default_rule()
{
    Rule r;
    r.name = "new rule";
    r.range = { Target::WRAM, 0x2000, 0x1FFFF };
    r.op = { .type = OpType::Random, .min = 0, .max = 255 };
    r.when = { .type = TriggerType::EveryFrames, .every = 120 };
    r.count = 1;
    return r;
}

SizedMemory::SizedMemory()
{
    for (auto t : kAllTargets)
        sizes[size_t(t)] = nominal_size(t);
}

} // namespace corrupt
