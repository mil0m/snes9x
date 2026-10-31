#include "rule_text.h"

#include <cstdio>

namespace corrupt
{

namespace
{

std::string hex4(uint32_t v)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%04X", v);
    return buf;
}

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
            return "swap " + hex4(*op.a) + " <-> " + hex4(*op.b) + (op.width == 2 ? ", 16-bit" : "");
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
    if (!rule.region.empty())
        return where + " " + rule.region;
    return where + " " + hex4(rule.range.first) + ".." + hex4(rule.range.last);
}

std::string describe_rule(const Rule &rule)
{
    return describe_range(rule) + ": " + describe_op(rule) + ", " + describe_trigger(rule.when) +
           (rule.avoid_hot ? ", avoiding danger" : "");
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
