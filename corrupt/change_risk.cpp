#include "change_risk.h"
#include "dossier.h"
#include "opcodes.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <vector>

namespace corrupt
{

const char *feed_name(Feed f)
{
    switch (f)
    {
    case Feed::Variable: return "variable";
    case Feed::Scratch: return "scratch";
    case Feed::Hardware: return "hardware";
    case Feed::Index: return "index";
    case Feed::LoopCount: return "loop count";
    case Feed::LoopBound: return "loop bound";
    case Feed::Condition: return "condition";
    case Feed::Pointer: return "pointer";
    case Feed::Argument: return "argument";
    case Feed::Overwritten: return "overwritten";
    case Feed::LoopBranch: return "loop branch";
    case Feed::ForwardBranch: return "forward branch";
    case Feed::UnseenPath: return "unseen path";
    case Feed::WaitLoop: return "wait loop";
    case Feed::WidthPath: return "width path";
    case Feed::StackPath: return "stack path";
    case Feed::ResultUsed: return "result used";
    case Feed::ResultUnused: return "result unused";
    case Feed::Unknown: return "unknown";
    }
    return "?";
}

namespace
{

struct Step
{
    uint32_t at = 0;
    int length = 0;
    uint8_t op = 0;
    std::string_view m;
    Mode mode = Mode::Implied;
    std::string text;
    std::optional<uint32_t> address;
};

bool any(std::string_view m, std::initializer_list<std::string_view> list)
{
    return std::find(list.begin(), list.end(), m) != list.end();
}

bool is_branch(std::string_view m)
{
    return any(m, { "BNE", "BEQ", "BCC", "BCS", "BPL", "BMI", "BVC", "BVS" });
}

bool indexed_by_x(Mode md)
{
    return md == Mode::AbsoluteX || md == Mode::DirectX || md == Mode::LongX || md == Mode::AbsIndX ||
           md == Mode::DirectIndX;
}
bool indexed_by_y(Mode md)
{
    return md == Mode::AbsoluteY || md == Mode::DirectY || md == Mode::DirectIndY || md == Mode::DirectLongY ||
           md == Mode::StackIndY;
}
bool through_pointer(Mode md)
{
    return md == Mode::DirectInd || md == Mode::DirectIndX || md == Mode::DirectIndY || md == Mode::DirectLong ||
           md == Mode::DirectLongY;
}

std::string hexa(uint32_t v)
{
    char buf[16];
    snprintf(buf, sizeof buf, "$%02X:%04X", (v >> 16) & 0xFF, v & 0xFFFF);
    return buf;
}

// A branch flipped swaps which way each case goes; both ways start from the same registers
// and stack, so what matters is what the ways are: one that never ran, a wait that can last
// forever, a block that leaves the register width or the stack different at the join.
template <class StepAt>
FeedReading branch_reading(const ChangeOption &option, std::span<const uint8_t> rom, std::span<const uint8_t> flags,
                           StepAt step_at)
{
    std::string_view m = kOpcodes[option.opcode].mnemonic;
    int8_t rel = option.at + 1 < rom.size() ? int8_t(rom[option.at + 1]) : 0;
    uint32_t fall = option.at + 2;
    int64_t target64 = int64_t(fall) + rel;
    if (target64 < 0 || uint64_t(target64) >= rom.size())
        return { Feed::Unknown, "branches out of the ROM" };
    uint32_t target = uint32_t(target64);
    auto ran = [&](uint32_t at) { return at < flags.size() && (flags[at] & codeflag::Opcode); };
    bool back = rel < 0;
    if (!ran(target) || !ran(fall))
        return { Feed::UnseenPath, std::string(!ran(target) ? "the branch was never taken" : "the branch was always taken") +
                                       " while the tracer watched: flipped, the way that never ran runs every time" };

    // The block between: a loop's body (back) or the block the branch skips (forward).
    uint32_t from = back ? target : fall, to = back ? option.at : target;
    int pushes = 0, stores = 0, calls = 0;
    bool width_unknown = false, leaves = false, readable = true;
    // Per width bit (M, X): the first and the last REP/SEP that touch it. REP first and SEP last
    // (or the other way) puts it back as it was; only one kind leaves it changed.
    char first[2] = { 0, 0 }, last[2] = { 0, 0 };
    for (uint32_t at = from; at < to;)
    {
        auto s = step_at(at);
        if (!s)
        {
            readable = false;
            break;
        }
        auto sm = s->m;
        if (any(sm, { "PHA", "PHX", "PHY", "PHP", "PHB", "PHD", "PHK", "PEA", "PEI", "PER" }))
            pushes++;
        else if (any(sm, { "PLA", "PLX", "PLY", "PLP", "PLB", "PLD" }))
            pushes--;
        if ((sm == "REP" || sm == "SEP") && s->at + 1 < rom.size())
            for (int b = 0; b < 2; b++)
                if (rom[s->at + 1] & (b == 0 ? 0x20 : 0x10))
                {
                    if (!first[b])
                        first[b] = sm[0];
                    last[b] = sm[0];
                }
        if (sm == "PLP" || sm == "XCE")
            width_unknown = true;
        if (op_class(s->op) == OpClass::Store || any(sm, { "INC", "DEC", "TSB", "TRB", "MVN", "MVP" }))
            stores++;
        if (any(sm, { "JSR", "JSL" }))
            calls++;
        if (any(sm, { "RTS", "RTL", "RTI", "JMP", "JML", "BRA", "BRL" }) && s->at + uint32_t(s->length) >= to)
            leaves = true; // the block ends by going elsewhere: it doesn't fall into the join
        at = s->at + uint32_t(s->length);
    }
    bool width_changed = false;
    for (int b = 0; b < 2; b++)
        width_changed |= first[b] && first[b] == last[b];
    if (back && readable && stores == 0 && calls == 0 && to - from <= 16)
        return { Feed::WaitLoop, "a loop that only reads and checks (" + std::string(m) +
                                     " back): it waits for something; flipped, it can wait forever" };
    if (readable && !leaves && (width_changed || width_unknown))
        return { Feed::WidthPath, std::string(back ? "the loop" : "the skipped block") +
                                      " changes the register width (REP/SEP): code after it then runs at a width it "
                                      "never ran at" };
    if (readable && !leaves && pushes != 0)
        return { Feed::StackPath, std::string(back ? "the loop" : "the skipped block") + " leaves the stack " +
                                      (pushes > 0 ? "deeper" : "shallower") + ": a later pull or return finds the wrong value" };
    if (back)
        return { Feed::LoopBranch, "both ways ran; branches back: the loop runs once, or for a different count" };
    return { Feed::ForwardBranch, "both ways ran; swaps which case skips the block" };
}

} // namespace

FeedReading feed_of(const ChangeOption &option, const FunctionBody &body, std::span<const uint8_t> rom,
                    std::span<const uint8_t> flags, const RomAddresses &where)
{
    auto step_at = [&](uint32_t at) -> std::optional<Step> {
        auto ins = instruction_at(rom, flags, at, where);
        if (!ins || ins->start != at)
            return std::nullopt;
        Step s;
        s.at = at;
        s.length = ins->length;
        s.op = ins->opcode;
        s.m = kOpcodes[s.op].mnemonic;
        s.mode = kOpcodes[s.op].mode;
        s.text = ins->text;
        s.address = ins->address;
        return s;
    };
    // The straight-line code after the option, as the body holds it (no gaps).
    std::vector<Step> after;
    {
        uint32_t next = option.at + option.length;
        for (auto &c : body.code)
        {
            if (c.at < next)
                continue;
            if (c.at != next || after.size() == 10)
                break;
            auto s = step_at(c.at);
            if (!s)
                break;
            after.push_back(*s);
            next = c.at + uint32_t(s->length);
        }
    }
    // Direct-page addresses the body reads as pointers.
    std::set<uint32_t> pointers;
    for (auto &c : body.code)
        if (auto s = step_at(c.at); s && through_pointer(s->mode) && c.at + 1 < rom.size())
            pointers.insert(rom[c.at + 1]);

    auto stored = [&](const Step &s) -> FeedReading {
        uint8_t bank = s.address ? uint8_t(*s.address >> 16) : 0;
        auto a = operand_access(rom, s.at, s.length, bank);
        if (through_pointer(s.mode))
            return { Feed::Variable, s.m == "STA" ? "stored through a pointer" : "stored through a pointer" };
        if (!a)
            return { Feed::Unknown, "stored somewhere code can't tell (" + s.text + ")" };
        if (a->kind == AccessKind::Hardware && a->address >= 0x4300 && (a->address & 0xF) >= 2 && (a->address & 0xF) <= 9)
            return { Feed::Pointer, "written to a DMA address or size ($" + hexa(a->address).substr(4) + ")" };
        if (a->kind == AccessKind::Hardware)
        {
            auto name = register_name(uint16_t(a->address));
            return { Feed::Hardware, "written to the register $" + hexa(a->address).substr(4) +
                                         (name.empty() ? "" : " (" + name + ")") };
        }
        if (a->direct && pointers.count(a->address & 0xFF))
            return { Feed::Pointer, "stored at " + hexa(a->address) + ", which the function reads as a pointer" };
        if (a->direct && (a->address & 0xFF) < 0x20 && !a->indexed)
            return { Feed::Scratch, "stored into the temporary byte " + hexa(a->address) + ", which code uses for many things" };
        return { Feed::Variable, "stored into " + hexa(a->address) + (a->indexed ? " (an array)" : "") };
    };
    auto branch_feed = [&](const Step &s, Feed back, Feed forward, const std::string &what) -> FeedReading {
        int8_t rel = s.at + 1 < rom.size() ? int8_t(rom[s.at + 1]) : 0;
        if (rel < 0)
            return { back, what + ", then " + std::string(s.m) + " back: the end of a loop" };
        return { forward, what + ", then " + std::string(s.m) + " forward" };
    };

    std::string_view m = kOpcodes[option.opcode].mnemonic;
    Mode mode = kOpcodes[option.opcode].mode;
    enum class Reg
    {
        A,
        X,
        Y,
        Flags,
        Carry
    } reg = Reg::A;

    switch (option.kind)
    {
    case ChangeKind::Branch:
        return branch_reading(option, rom, flags, step_at);
    case ChangeKind::Skip:
    case ChangeKind::Redirect:
    {
        if (option.kind == ChangeKind::Redirect)
            return { Feed::Unknown, "a redirected call" };
        bool a = false, x = false, y = false, nz = false, c = false; // written again since the call
        for (auto &s : after)
        {
            auto sm = s.m;
            bool imm = s.mode == Mode::ImmM || s.mode == Mode::ImmX || s.mode == Mode::Imm8;
            (void)imm;
            // What it reads.
            bool reads_a = any(sm, { "STA", "TAX", "TAY", "TCD", "TCS", "PHA", "XBA", "CMP", "ADC", "SBC", "AND", "ORA",
                                     "EOR", "BIT" }) ||
                           (s.mode == Mode::Accumulator);
            bool reads_x = any(sm, { "STX", "TXA", "TXY", "TXS", "PHX", "CPX", "INX", "DEX" }) || indexed_by_x(s.mode);
            bool reads_y = any(sm, { "STY", "TYA", "TYX", "PHY", "CPY", "INY", "DEY" }) || indexed_by_y(s.mode);
            bool reads_c = any(sm, { "ADC", "SBC", "ROL", "ROR", "BCC", "BCS" });
            bool reads_nz = any(sm, { "BEQ", "BNE", "BMI", "BPL", "BVC", "BVS", "PHP" });
            if ((reads_a && !a) || (reads_x && !x) || (reads_y && !y) || (reads_c && !c) || (reads_nz && !nz))
            {
                std::string what = reads_a && !a ? "A" : reads_x && !x ? "X" : reads_y && !y ? "Y" : reads_c && !c ? "the carry" : "the flags";
                return { Feed::ResultUsed, "the caller reads " + what + " right after (" + s.text + ")" };
            }
            if (any(sm, { "RTS", "RTL", "RTI" }))
                return { Feed::ResultUsed, "the caller returns right after, handing the result on" };
            if (is_branch(sm) || any(sm, { "JMP", "JML", "BRA", "BRL", "JSR", "JSL" }))
                break;
            // What it sets.
            if (any(sm, { "LDA", "PLA", "TXA", "TYA", "TDC", "TSC" }))
                a = true;
            if (any(sm, { "LDX", "PLX", "TAX", "TYX", "TSX" }))
                x = true;
            if (any(sm, { "LDY", "PLY", "TAY", "TXY" }))
                y = true;
            if (any(sm, { "CLC", "SEC", "ADC", "SBC", "CMP", "CPX", "CPY", "ASL", "LSR", "ROL", "ROR", "PLP" }))
                c = true;
            if (op_class(s.op) == OpClass::Load || op_class(s.op) == OpClass::Arith || op_class(s.op) == OpClass::Logic ||
                op_class(s.op) == OpClass::Compare || op_class(s.op) == OpClass::Shift ||
                (op_class(s.op) == OpClass::Transfer && sm != "TXS" && sm != "TCS"))
                nz = true;
            if (a && x && y && c && nz)
                return { Feed::ResultUnused, "the caller sets A, X, Y and the flags again before using them" };
        }
        if (a && nz && c)
            return { Feed::ResultUnused, "the caller sets A and the flags again before using them" };
        return { Feed::Unknown, "can't tell whether the caller uses the result" };
    }
    case ChangeKind::Number:
        if (any(m, { "LDX" }))
            reg = Reg::X;
        else if (any(m, { "LDY" }))
            reg = Reg::Y;
        else if (any(m, { "CMP", "CPX", "CPY", "BIT" }))
            reg = Reg::Flags;
        else
            reg = Reg::A;
        break;
    case ChangeKind::Math:
        if (any(m, { "INX", "DEX" }))
            reg = Reg::X;
        else if (any(m, { "INY", "DEY" }))
            reg = Reg::Y;
        else if (any(m, { "CLC", "SEC" }))
            reg = Reg::Carry;
        else if (any(m, { "INC", "DEC", "ASL", "LSR", "ROL", "ROR" }) && mode != Mode::Accumulator)
        {
            Step self;
            self.at = option.at;
            self.length = option.length;
            self.op = option.opcode;
            self.m = m;
            self.mode = mode;
            self.text = option.text;
            self.address = option.address;
            auto r = stored(self);
            r.why = "changes the memory directly: " + r.why;
            return r;
        }
        else
            reg = Reg::A;
        break;
    }

    // A counter stepped right before a branch back counts that loop.
    if (option.kind == ChangeKind::Math && (reg == Reg::X || reg == Reg::Y) && !after.empty() && is_branch(after[0].m) &&
        after[0].at + 1 < rom.size() && int8_t(rom[after[0].at + 1]) < 0)
        return { Feed::LoopCount, "counted down or up, then a branch back: a loop count" };
    std::string start = reg == Reg::Flags ? "compared" : reg == Reg::Carry ? "the carry" : "the value";
    for (size_t k = 0; k < after.size(); k++)
    {
        auto &s = after[k];
        auto sm = s.m;
        if (sm == "JMP" || sm == "JML" || sm == "BRA" || sm == "BRL")
            return { Feed::Unknown, "the trail goes on elsewhere (" + s.text + ")" };
        if (any(sm, { "JSR", "JSL" }) && s.mode != Mode::AbsIndX)
            return { Feed::Argument, start + " is handed to a call (" + s.text + ")" };
        if (any(sm, { "RTS", "RTL", "RTI" }))
            return { Feed::Argument, start + " is returned to the caller" };
        switch (reg)
        {
        case Reg::Flags:
            if (is_branch(sm))
                return branch_feed(s, Feed::LoopBound, Feed::Condition, "compared");
            if (op_class(s.op) != OpClass::Store && op_class(s.op) != OpClass::Stack && op_class(s.op) != OpClass::Width)
                return { Feed::Overwritten, "the comparison is replaced before a branch uses it" };
            break;
        case Reg::Carry:
            if (any(sm, { "ADC", "SBC", "ROL", "ROR" }))
            {
                reg = Reg::A;
                start = "the sum";
            }
            else if (sm == "BCC" || sm == "BCS")
                return branch_feed(s, Feed::LoopBound, Feed::Condition, "the carry is tested");
            else if (any(sm, { "CLC", "SEC", "CMP", "CPX", "CPY", "ASL", "LSR", "PLP" }))
                return { Feed::Overwritten, "the carry is set again before anything uses it" };
            break;
        case Reg::A:
            if (any(sm, { "STA" }))
                return stored(s);
            if (any(sm, { "TAX", "TAY" }))
                return { Feed::Index, std::string(sm) + ": becomes an index" };
            if (any(sm, { "TCD", "TCS" }))
                return { Feed::Pointer, std::string(sm) + ": becomes the " + (sm == "TCD" ? "direct page" : "stack") };
            if (sm == "PHA")
                return { Feed::Argument, "pushed on the stack" };
            if (any(sm, { "CMP", "BIT" }))
                reg = Reg::Flags;
            else if (is_branch(sm))
                return branch_feed(s, Feed::LoopBound, Feed::Condition, "tested");
            else if (sm == "MVN" || sm == "MVP")
                return { Feed::LoopCount, "the length of a block move" };
            else if (sm == "XBA")
                return { Feed::Unknown, "moved to the high byte (XBA), used later" };
            else if (any(sm, { "LDA", "PLA", "TXA", "TYA", "TDC", "TSC" }))
                return { Feed::Overwritten, "replaced by " + s.text + " before anything uses it" };
            break;
        case Reg::X:
        case Reg::Y:
        {
            bool x = reg == Reg::X;
            if ((x && indexed_by_x(s.mode)) || (!x && indexed_by_y(s.mode)))
                return { Feed::Index, std::string(s.mode == Mode::AbsIndX ? "picks a jump-table entry" : "used as an index") +
                                          " (" + s.text + ")" };
            if ((x && sm == "STX") || (!x && sm == "STY"))
                return stored(s);
            if ((x && (sm == "INX" || sm == "DEX")) || (!x && (sm == "INY" || sm == "DEY")))
            {
                if (k + 1 < after.size() && is_branch(after[k + 1].m) && after[k + 1].at + 1 < rom.size() &&
                    int8_t(rom[after[k + 1].at + 1]) < 0)
                    return { Feed::LoopCount, "counted down or up, then a branch back: a loop count" };
                break;
            }
            if ((x && sm == "CPX") || (!x && sm == "CPY"))
            {
                reg = Reg::Flags;
                break;
            }
            if ((x && sm == "TXS"))
                return { Feed::Pointer, "TXS: becomes the stack" };
            if ((x && sm == "TXA") || (!x && sm == "TYA"))
            {
                reg = Reg::A;
                break;
            }
            if ((x && sm == "TXY") || (!x && sm == "TYX"))
            {
                reg = x ? Reg::Y : Reg::X;
                break;
            }
            if ((x && sm == "PHX") || (!x && sm == "PHY"))
                return { Feed::Argument, "pushed on the stack" };
            if (sm == "MVN" || sm == "MVP")
                return { Feed::Pointer, "an address of a block move" };
            if (is_branch(sm) && k == 0)
                return branch_feed(s, Feed::LoopBound, Feed::Condition, "tested");
            if ((x && any(sm, { "LDX", "PLX", "TAX", "TYX", "TSX" })) || (!x && any(sm, { "LDY", "PLY", "TAY", "TXY" })))
                return { Feed::Overwritten, "replaced by " + s.text + " before anything uses it" };
            break;
        }
        }
    }
    return { Feed::Unknown, after.size() == 10 ? "the trail goes cold" : "the trail ends at a branch or the body's end" };
}

} // namespace corrupt

namespace corrupt
{

// Measured with corrupt-smoke --risk (docs/corruption/2026-11-02-function-changes-plan.md,
// "Changes that don't crash"): the share of single changes the game ran that left it frozen
// or dead within 30 s, on Goof Troop, Pocky & Rocky and FE4 together (952 changes of every
// kind, and 533 more branch flips measured alone). -1: fewer than 10 seen.
int measured_failures(Feed f)
{
    switch (f)
    {
    case Feed::Variable: return 4;       // 123 changes
    case Feed::Condition: return 4;      // 153
    case Feed::ResultUsed: return 6;     // 64
    case Feed::ResultUnused: return 7;   // 14
    case Feed::UnseenPath: return 5;     // 235
    case Feed::ForwardBranch: return 8;  // 369
    case Feed::LoopBranch: return 10;    // 52
    case Feed::WaitLoop: return 10;      // 74
    case Feed::WidthPath: return 0;      // 12
    case Feed::Unknown: return 10;       // 120
    case Feed::Scratch: return 16;       // 45
    case Feed::Index: return 21;         // 110
    case Feed::LoopBound: return 28;     // 43
    case Feed::Argument: return 37;      // 46
    case Feed::Pointer: return 40;       // 10
    case Feed::Hardware:                 // 6, none failed
    case Feed::Overwritten:              // 2
    case Feed::LoopCount:                // 7, 6 failed
    case Feed::StackPath: return -1;     // none seen
    }
    return -1;
}

Risk risk_of(Feed f)
{
    switch (f)
    {
    // Can't tell, or not seen: a stack change at the join reads as dangerous, but none
    // turned up in 533 branch flips.
    case Feed::Unknown:
    case Feed::StackPath: return Risk::Unclear;
    case Feed::LoopCount: return Risk::High; // 6 of the 7 seen failed
    // Alone, a way that never ran failed 5% of the time; six changes a minute kept the game
    // moving in 38 of 60 runs with them among the low-risk picks, 45 of 60 without.
    case Feed::UnseenPath: return Risk::Medium;
    case Feed::Hardware:
    case Feed::Overwritten: return Risk::Low;
    default: break;
    }
    int failed = measured_failures(f);
    return failed < 10 ? Risk::Low : failed <= 25 ? Risk::Medium : Risk::High;
}

const char *risk_name(Risk r)
{
    switch (r)
    {
    case Risk::Low: return "low";
    case Risk::Medium: return "medium";
    case Risk::High: return "high";
    case Risk::Unclear: return "unclear";
    }
    return "?";
}

HotMask low_risk_mask(const FunctionBodies &bodies, std::span<const uint8_t> rom, std::span<const uint8_t> flags,
                      const RomAddresses &where)
{
    HotMask mask;
    for (auto &[entry, body] : bodies)
        for (auto &o : change_options(body, rom, flags, where))
        {
            if (risk_of(feed_of(o, body, rom, flags, where).feed) != Risk::Low)
                continue;
            if (o.kind == ChangeKind::Number)
                for (int i = 1; i <= o.width; i++)
                    mask.add(Target::ROM, o.at + uint32_t(i));
            else if (o.kind == ChangeKind::Skip || o.kind == ChangeKind::Redirect)
                for (uint32_t i = 0; i < o.length; i++)
                    mask.add(Target::ROM, o.at + i);
            else
                mask.add(Target::ROM, o.at);
        }
    return mask;
}

} // namespace corrupt
