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
    {
        int8_t rel = option.at + 1 < rom.size() ? int8_t(rom[option.at + 1]) : 0;
        if (rel < 0)
            return { Feed::LoopBranch, "branches back: a loop's exit or continue" };
        return { Feed::ForwardBranch, "branches forward over a block" };
    }
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
