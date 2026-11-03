#include "dossier.h"
#include "function_body.h"
#include "opcodes.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <unordered_map>

namespace corrupt
{

namespace
{

std::string hex(uint32_t v, int digits)
{
    char buf[16];
    snprintf(buf, sizeof buf, "%0*X", digits, v);
    return buf;
}

std::string cpu(uint32_t address) // $BB:AAAA
{
    return "$" + hex((address >> 16) & 0xFF, 2) + ":" + hex(address & 0xFFFF, 4);
}

std::string thousands(uint64_t n)
{
    std::string s = std::to_string(n);
    for (int i = int(s.size()) - 3; i > 0; i -= 3)
        s.insert(size_t(i), ",");
    return s;
}

bool is_any(std::string_view m, std::initializer_list<std::string_view> list)
{
    return std::find(list.begin(), list.end(), m) != list.end();
}

// Read, write or both, by mnemonic, for an instruction that touches memory.
void read_write(std::string_view m, Access &a)
{
    if (is_any(m, { "STA", "STX", "STY", "STZ" }))
        a.write = true;
    else if (is_any(m, { "INC", "DEC", "ASL", "LSR", "ROL", "ROR", "TSB", "TRB" }))
        a.read = a.write = true;
    else
        a.read = true;
}

std::optional<Access> classify(uint32_t address)
{
    uint8_t bank = (address >> 16) & 0xFF;
    uint16_t low = address & 0xFFFF;
    Access a;
    if (bank == 0x7E || bank == 0x7F)
    {
        a.kind = AccessKind::Ram;
        a.address = address;
        return a;
    }
    if (bank >= 0x70 && bank <= 0x7D && low < 0x8000)
    {
        a.kind = AccessKind::Save;
        a.address = address;
        return a;
    }
    bool system = bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF);
    if (system && low < 0x2000)
    {
        a.kind = AccessKind::Ram;
        a.address = 0x7E0000 | low;
        return a;
    }
    if (system && ((low >= 0x2100 && low <= 0x21FF) || low == 0x4016 || low == 0x4017 ||
                   (low >= 0x4200 && low <= 0x43FF)))
    {
        a.kind = AccessKind::Hardware;
        a.address = low;
        return a;
    }
    if (low >= 0x8000 || (!system && bank >= 0x40))
    {
        a.kind = AccessKind::Rom;
        a.address = address;
        return a;
    }
    return std::nullopt; // expansion, open bus: nothing to say
}

} // namespace

std::optional<Access> operand_access(std::span<const uint8_t> rom, uint32_t at, int length, uint8_t bank)
{
    if (at >= rom.size() || at + uint32_t(length) > rom.size())
        return std::nullopt;
    uint8_t op = rom[at];
    const auto &info = kOpcodes[op];
    std::string_view m = info.mnemonic;
    if (op_class(op) == OpClass::Jump || op_class(op) == OpClass::Branch || is_any(m, { "PEA", "PEI", "PER" }))
        return std::nullopt;
    auto byte = [&](int i) { return uint32_t(rom[at + uint32_t(i)]); };
    std::optional<Access> a;
    bool direct = false, indexed = false;
    switch (info.mode)
    {
    case Mode::Direct:
    case Mode::DirectX:
    case Mode::DirectY:
    case Mode::DirectInd:
    case Mode::DirectIndX:
    case Mode::DirectIndY:
    case Mode::DirectLong:
    case Mode::DirectLongY:
        if (length < 2)
            return std::nullopt;
        a = classify(byte(1)); // D = 0: the low page of work RAM
        direct = true;
        indexed = info.mode != Mode::Direct;
        break;
    case Mode::Absolute:
    case Mode::AbsoluteX:
    case Mode::AbsoluteY:
        if (length < 3)
            return std::nullopt;
        a = classify((uint32_t(bank) << 16) | byte(1) | (byte(2) << 8));
        indexed = info.mode != Mode::Absolute;
        break;
    case Mode::Long:
    case Mode::LongX:
        if (length < 4)
            return std::nullopt;
        a = classify(byte(1) | (byte(2) << 8) | (byte(3) << 16));
        indexed = info.mode == Mode::LongX;
        break;
    default: return std::nullopt;
    }
    if (!a)
        return std::nullopt;
    // Through a pointer at a direct-page address: the access is to the pointer itself.
    bool pointer = info.mode == Mode::DirectInd || info.mode == Mode::DirectIndX || info.mode == Mode::DirectIndY ||
                   info.mode == Mode::DirectLong || info.mode == Mode::DirectLongY;
    if (pointer)
        a->read = true; // the pointer is read; where it points isn't known from code
    else
        read_write(m, *a);
    a->direct = direct;
    a->indexed = indexed;
    return a;
}

std::string register_name(uint16_t address)
{
    static const std::map<uint16_t, const char *> names = {
        { 0x2100, "INIDISP screen on/brightness" }, { 0x2101, "OBSEL sprite size/base" },
        { 0x2102, "OAMADDL" }, { 0x2103, "OAMADDH" }, { 0x2104, "OAMDATA sprite write" },
        { 0x2105, "BGMODE" }, { 0x2106, "MOSAIC" }, { 0x2107, "BG1SC" }, { 0x2108, "BG2SC" },
        { 0x2109, "BG3SC" }, { 0x210A, "BG4SC" }, { 0x210B, "BG12NBA" }, { 0x210C, "BG34NBA" },
        { 0x210D, "BG1HOFS scroll" }, { 0x210E, "BG1VOFS scroll" }, { 0x210F, "BG2HOFS scroll" },
        { 0x2110, "BG2VOFS scroll" }, { 0x2111, "BG3HOFS scroll" }, { 0x2112, "BG3VOFS scroll" },
        { 0x2113, "BG4HOFS scroll" }, { 0x2114, "BG4VOFS scroll" }, { 0x2115, "VMAIN" },
        { 0x2116, "VMADDL VRAM addr" }, { 0x2117, "VMADDH VRAM addr" }, { 0x2118, "VMDATAL VRAM write" },
        { 0x2119, "VMDATAH VRAM write" }, { 0x211A, "M7SEL" }, { 0x211B, "M7A mode 7 / multiply" },
        { 0x211C, "M7B mode 7 / multiply" }, { 0x211D, "M7C" }, { 0x211E, "M7D" }, { 0x211F, "M7X" },
        { 0x2120, "M7Y" }, { 0x2121, "CGADD palette addr" }, { 0x2122, "CGDATA palette write" },
        { 0x2123, "W12SEL window" }, { 0x2124, "W34SEL window" }, { 0x2125, "WOBJSEL window" },
        { 0x2126, "WH0 window" }, { 0x2127, "WH1 window" }, { 0x2128, "WH2 window" }, { 0x2129, "WH3 window" },
        { 0x212A, "WBGLOG" }, { 0x212B, "WOBJLOG" }, { 0x212C, "TM main screen layers" },
        { 0x212D, "TS sub screen layers" }, { 0x212E, "TMW" }, { 0x212F, "TSW" },
        { 0x2130, "CGWSEL colour math" }, { 0x2131, "CGADSUB colour math" }, { 0x2132, "COLDATA fixed colour" },
        { 0x2133, "SETINI" }, { 0x2134, "MPYL multiply result" }, { 0x2135, "MPYM multiply result" },
        { 0x2136, "MPYH multiply result" }, { 0x2137, "SLHV latch counters" }, { 0x2138, "OAMDATAREAD" },
        { 0x2139, "VMDATALREAD" }, { 0x213A, "VMDATAHREAD" }, { 0x213B, "CGDATAREAD" },
        { 0x213C, "OPHCT" }, { 0x213D, "OPVCT" }, { 0x213E, "STAT77" }, { 0x213F, "STAT78" },
        { 0x2140, "APUIO0 sound CPU port" }, { 0x2141, "APUIO1 sound CPU port" },
        { 0x2142, "APUIO2 sound CPU port" }, { 0x2143, "APUIO3 sound CPU port" },
        { 0x2180, "WMDATA WRAM port" }, { 0x2181, "WMADDL" }, { 0x2182, "WMADDM" }, { 0x2183, "WMADDH" },
        { 0x4016, "JOYSER0 joypad (manual)" }, { 0x4017, "JOYSER1 joypad (manual)" },
        { 0x4200, "NMITIMEN NMI/IRQ/auto-joypad enable" }, { 0x4201, "WRIO" },
        { 0x4202, "WRMPYA multiply" }, { 0x4203, "WRMPYB multiply" }, { 0x4204, "WRDIVL divide" },
        { 0x4205, "WRDIVH divide" }, { 0x4206, "WRDIVB divide" }, { 0x4207, "HTIMEL IRQ" },
        { 0x4208, "HTIMEH IRQ" }, { 0x4209, "VTIMEL IRQ" }, { 0x420A, "VTIMEH IRQ" },
        { 0x420B, "MDMAEN start DMA" }, { 0x420C, "HDMAEN HDMA channels" }, { 0x420D, "MEMSEL fast ROM" },
        { 0x4210, "RDNMI" }, { 0x4211, "TIMEUP IRQ ack" }, { 0x4212, "HVBJOY status" },
        { 0x4213, "RDIO" }, { 0x4214, "RDDIVL divide result" }, { 0x4215, "RDDIVH divide result" },
        { 0x4216, "RDMPYL multiply result" }, { 0x4217, "RDMPYH multiply result" },
        { 0x4218, "JOY1L joypad 1" }, { 0x4219, "JOY1H joypad 1" }, { 0x421A, "JOY2L joypad 2" },
        { 0x421B, "JOY2H joypad 2" }, { 0x421C, "JOY3L" }, { 0x421D, "JOY3H" }, { 0x421E, "JOY4L" },
        { 0x421F, "JOY4H" },
    };
    if (auto it = names.find(address); it != names.end())
        return it->second;
    if (address >= 0x4300 && address <= 0x437F)
    {
        static const char *fields[16] = { "DMAP mode", "BBAD B-bus register", "A1TL A-bus addr", "A1TH A-bus addr",
                                          "A1B A-bus bank", "DASL size", "DASH size", "DASB HDMA bank",
                                          "A2AL HDMA table", "A2AH HDMA table", "NLTR HDMA lines", "", "", "", "", "" };
        auto field = fields[address & 0xF];
        if (*field)
            return "DMA" + std::to_string((address >> 4) & 7) + " " + field;
    }
    return {};
}

namespace
{

struct Info
{
    const Function *fn = nullptr;
    FunctionBody body;
    std::set<uint32_t> callers; // entries of functions that call it
    std::map<uint32_t, uint32_t> callees; // entry -> hits
    std::vector<size_t> tables; // jump tables dispatched from it
    std::vector<std::pair<size_t, size_t>> via; // reached through: (table, entry)
};

struct RamUse
{
    std::set<uint32_t> readers, writers; // function entries
};

} // namespace

std::string make_dossier(const DossierInput &in)
{
    std::string out;
    auto line = [&](const std::string &s) { out += s + "\n"; };

    // Every function, by entry, and every instruction to the function it belongs to.
    std::unordered_map<uint32_t, Info> info;
    for (auto &f : in.functions)
    {
        auto &i = info[f.entry];
        i.fn = &f;
        i.body = function_body(in.rom, in.flags, f.entry, in.where);
    }
    // Bodies overlap (shared tails, code jumped into): an instruction belongs to the nearest
    // entry at or before it, else the nearest after it.
    std::unordered_map<uint32_t, uint32_t> owner; // instruction offset -> entry
    auto nearer = [](uint32_t at, uint32_t a, uint32_t b) {
        if ((a <= at) != (b <= at))
            return a <= at;
        return a <= at ? a > b : a < b;
    };
    for (auto &[entry, i] : info)
        for (auto &c : i.body.code)
        {
            auto [it, fresh] = owner.emplace(c.at, entry);
            if (!fresh && nearer(c.at, entry, it->second))
                it->second = entry;
        }
    std::unordered_map<uint32_t, uint32_t> call_at; // call site -> its (first) target
    for (auto &[key, seen] : in.calls.all())
        call_at.emplace(key.site, key.target);
    auto label = [&](uint32_t entry) {
        auto where = in.where.cpu_address(entry);
        std::string s = where ? cpu(*where) : "ROM 0x" + hex(entry, 6);
        auto name = in.names.label(entry);
        if (!name.empty())
            s += " \"" + name + "\"";
        return s;
    };

    // Who calls whom, from the call log: the call site's function calls the target.
    for (auto &[key, seen] : in.calls.all())
    {
        auto o = owner.find(key.site);
        if (o == owner.end())
            continue;
        auto t = info.find(key.target);
        if (t != info.end())
            t->second.callers.insert(o->second);
        info[o->second].callees[key.target] += seen.hits;
    }
    for (size_t t = 0; t < in.tables.size(); t++)
    {
        if (auto o = owner.find(in.tables[t].site); o != owner.end())
            info[o->second].tables.push_back(t);
        for (size_t e = 0; e < in.tables[t].entries.size(); e++)
            if (auto o = in.offsets.offset(in.tables[t].entries[e].target); o && info.count(*o))
                info[*o].via.push_back({ t, e });
    }

    // Which functions go in: the busiest, every named one, every jump-table target.
    std::vector<const Function *> ranked;
    for (auto &f : in.functions)
        ranked.push_back(&f);
    std::stable_sort(ranked.begin(), ranked.end(), [](auto *a, auto *b) {
        return a->frames != b->frames ? a->frames > b->frames : a->entry < b->entry;
    });
    std::set<uint32_t> chosen;
    for (size_t i = 0; i < ranked.size() && i < in.max_functions; i++)
        chosen.insert(ranked[i]->entry);
    for (auto &f : in.functions)
        if (!in.names.label(f.entry).empty())
            chosen.insert(f.entry);
    for (auto &t : in.tables)
        for (auto &e : t.entries)
            if (auto o = in.offsets.offset(e.target); o && info.count(*o) && info[*o].fn)
                chosen.insert(*o);

    // Memory uses, per chosen function and for the RAM index (every function).
    std::map<uint32_t, RamUse> ram;
    auto accesses_of = [&](const Info &i) {
        std::vector<Access> list;
        for (auto &c : i.body.code)
        {
            auto where = in.where.cpu_address(c.at);
            uint8_t bank = where ? uint8_t(*where >> 16) : 0;
            if (auto a = operand_access(in.rom, c.at, c.length, bank))
                list.push_back(*a);
        }
        return list;
    };
    std::unordered_map<uint32_t, std::vector<Access>> uses;
    for (auto &[entry, i] : info)
    {
        if (!i.fn)
            continue;
        auto list = accesses_of(i);
        for (auto &a : list)
            if (a.kind == AccessKind::Ram)
            {
                if (a.read)
                    ram[a.address].readers.insert(entry);
                if (a.write)
                    ram[a.address].writers.insert(entry);
            }
        if (chosen.count(entry))
            uses[entry] = std::move(list);
    }

    // The header.
    size_t code = 0, data = 0;
    for (auto f : in.flags)
    {
        code += (f & codeflag::Code) != 0;
        data += (f & (codeflag::Data | codeflag::Dma)) != 0;
    }
    line("# Game dossier: " + in.title);
    line("");
    line("What the tracer has learned about this game while people played it. Made by the app from the "
         "library's files, without any model; names in quotes are people's (with who) unless marked otherwise.");
    line("");
    line("- ROM: " + thousands(in.rom.size()) + " bytes; known as code " + thousands(code) + ", read as data " +
         thousands(data) + ".");
    line("- Functions seen called: " + thousands(in.functions.size()) + " (listed below: " +
         std::to_string(chosen.size()) + ", the busiest by frames run, every named one, every jump-table target).");
    line("- Jump tables: " + std::to_string(in.tables.size()) + ".");
    for (auto &s : in.sources)
        line("- From: " + s);
    line("");
    line("**What code alone can't tell.** Addresses come from the instructions themselves. The direct page is "
         "taken as $0000 and the data bank as the code's own bank, so `$12` reads as $7E:0012 and `$8000` as ROM "
         "in the code's bank; games that move either are read wrong there. Indexed and pointer operands give a "
         "base, not every byte touched. Only code that has run is here, and *frames* counts the frames a function "
         "was called in, across everyone's play.");
    line("");

    // Functions.
    line("## Functions");
    line("");
    std::vector<uint32_t> order(chosen.begin(), chosen.end());
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        auto fa = info[a].fn, fb = info[b].fn;
        uint64_t ra = fa ? fa->frames : 0, rb = fb ? fb->frames : 0;
        return ra != rb ? ra > rb : a < b;
    });
    for (auto entry : order)
    {
        auto &i = info[entry];
        if (!i.fn)
            continue;
        const char *type = i.fn->type == CallType::Long ? "JSL (returns RTL)"
                           : i.fn->type == CallType::Near ? "JSR (returns RTS)"
                                                         : "JSR and JSL";
        line("### " + label(entry) + " · ROM 0x" + hex(entry, 6));
        line("");
        line("- " + std::string(type) + "; called in " + thousands(i.fn->frames) + " frames, " +
             thousands(i.fn->calls) + " calls from " + std::to_string(i.fn->callers) + " call sites; body " +
             std::to_string(i.body.bytes) + " bytes" + (i.body.open_ended ? ", leaves through an indirect jump" : "") +
             (i.body.truncated ? ", cut at 4 KB" : "") + ".");
        if (!i.callers.empty())
        {
            std::string s;
            size_t n = 0;
            for (auto c : i.callers)
                if (n++ < 8)
                    s += (s.empty() ? "" : ", ") + label(c);
            if (i.callers.size() > 8)
                s += ", and " + std::to_string(i.callers.size() - 8) + " more";
            line("- Called from: " + s + ".");
        }
        if (!i.callees.empty())
        {
            std::vector<std::pair<uint32_t, uint32_t>> list(i.callees.begin(), i.callees.end());
            std::stable_sort(list.begin(), list.end(), [](auto &a, auto &b) { return a.second > b.second; });
            std::string s;
            for (size_t k = 0; k < list.size() && k < 12; k++)
                s += (s.empty() ? "" : ", ") + label(list[k].first);
            if (list.size() > 12)
                s += ", and " + std::to_string(list.size() - 12) + " more";
            line("- Calls: " + s + ".");
        }
        if (!i.via.empty())
        {
            std::string s;
            uint64_t hits = 0;
            for (size_t k = 0; k < i.via.size(); k++)
            {
                auto &[t, e] = i.via[k];
                hits += in.tables[t].entries[e].hits;
                if (k < 6)
                {
                    auto site = in.where.cpu_address(in.tables[t].site);
                    s += (s.empty() ? "" : ", ") + std::string("entry ") + std::to_string(e) + " of the table at " +
                         label(in.tables[t].first()) + " (from " + (site ? cpu(*site) : "?") + ")";
                }
            }
            if (i.via.size() > 6)
                s += ", and " + std::to_string(i.via.size() - 6) + " more";
            line("- Reached through jump tables: " + s +
                 (hits ? "; jumped to " + thousands(hits) + " times." : "; likely entries, not seen used yet."));
        }
        for (auto t : i.tables)
            line("- Dispatches through the jump table at " + label(in.tables[t].first()) + " (" +
                 std::to_string(in.tables[t].entries.size()) + " entries; see Jump tables).");
        // Memory, grouped.
        std::map<uint32_t, std::pair<bool, bool>> rams, hws, saves;
        std::set<uint32_t> roms;
        for (auto &a : uses[entry])
        {
            if (a.kind == AccessKind::Rom)
            {
                roms.insert(a.address);
                continue;
            }
            auto &slot = a.kind == AccessKind::Ram ? rams[a.address]
                         : a.kind == AccessKind::Hardware ? hws[a.address]
                                                          : saves[a.address];
            slot.first |= a.read;
            slot.second |= a.write;
        }
        auto rw = [](std::pair<bool, bool> p) { return p.first && p.second ? "rw" : p.second ? "w" : "r"; };
        if (!rams.empty())
        {
            std::string s;
            size_t n = 0;
            for (auto &[a, p] : rams)
                if (n++ < 40)
                    s += (s.empty() ? "" : ", ") + cpu(a) + " " + rw(p);
            if (rams.size() > 40)
                s += ", and " + std::to_string(rams.size() - 40) + " more";
            line("- RAM: " + s + ".");
        }
        if (!hws.empty())
        {
            std::string s;
            for (auto &[a, p] : hws)
            {
                auto name = register_name(uint16_t(a));
                s += (s.empty() ? "" : ", ") + std::string("$") + hex(a, 4) + (name.empty() ? "" : " " + name) + " " +
                     rw(p);
            }
            line("- Hardware: " + s + ".");
        }
        if (!saves.empty())
        {
            std::string s;
            for (auto &[a, p] : saves)
                s += (s.empty() ? "" : ", ") + cpu(a) + " " + rw(p);
            line("- Save RAM: " + s + ".");
        }
        if (!roms.empty())
        {
            std::string s;
            size_t n = 0;
            for (auto a : roms)
                if (n++ < 16)
                    s += (s.empty() ? "" : ", ") + cpu(a);
            if (roms.size() > 16)
                s += ", and " + std::to_string(roms.size() - 16) + " more";
            line("- ROM data read at: " + s + ".");
        }
        // The listing.
        line("");
        line("```");
        // From the entry on first, then any of its code that lies before the entry (reached by
        // a jump). A gap in addresses is marked: the code there was reached by a jump or branch.
        // Past the cut, only calls, jumps, returns and register accesses are kept.
        std::vector<const BodyInstruction *> listing;
        for (auto &c : i.body.code)
            if (c.at >= entry)
                listing.push_back(&c);
        for (auto &c : i.body.code)
            if (c.at < entry)
                listing.push_back(&c);
        size_t shown = 0, skipped = 0;
        uint32_t next = entry;
        for (auto *c : listing)
        {
            auto ins = instruction_at(in.rom, in.flags, c->at, in.where);
            if (!ins || ins->start != c->at)
                continue;
            std::string note;
            std::string_view m = kOpcodes[ins->opcode].mnemonic;
            auto cls = op_class(ins->opcode);
            if (auto t = call_at.find(c->at); t != call_at.end())
                note = label(t->second);
            else if (auto a = operand_access(in.rom, c->at, c->length, ins->address ? uint8_t(*ins->address >> 16) : 0);
                     a && a->kind == AccessKind::Hardware)
                note = register_name(uint16_t(a->address));
            bool key = cls == OpClass::Jump || cls == OpClass::Return || !note.empty() || m == "RTI";
            if (shown >= in.max_instructions && !key)
            {
                skipped++;
                continue;
            }
            if (skipped)
            {
                line("; ... " + std::to_string(skipped) + (skipped == 1 ? " instruction" : " instructions") + " left out");
                skipped = 0;
            }
            else if (c->at != next)
                line(c->at < entry ? "; --- before the entry, reached by a jump:" : "; --- elsewhere, reached by a jump or branch:");
            std::string text = (ins->address ? cpu(*ins->address) : "0x" + hex(c->at, 6)) + "  " + ins->text;
            if (owner[c->at] != entry)
                note += std::string(note.empty() ? "" : " · ") + "shared with " + label(owner[c->at]);
            if (!note.empty())
                text += "    ; " + note;
            line(text);
            shown++;
            next = c->at + uint32_t(std::max(1, ins->length));
        }
        if (skipped)
            line("; ... " + std::to_string(skipped) + (skipped == 1 ? " instruction" : " instructions") + " left out");
        line("```");
        line("");
    }

    // Jump tables.
    line("## Jump tables");
    line("");
    if (in.tables.empty())
        line("None found yet.");
    for (auto &t : in.tables)
    {
        auto o = owner.find(t.site);
        auto site = in.where.cpu_address(t.site);
        line("### Table at " + label(t.first()) + ", dispatched from " + (site ? cpu(*site) : "?") +
             (o != owner.end() ? " in " + label(o->second) : std::string()));
        line("");
        line(std::to_string(t.entries.size()) + " entries of " + std::to_string(t.stride) + " bytes" +
             (t.kind == JumpKind::ThroughRam ? ", copied to RAM before the jump" : "") + ":");
        line("");
        for (size_t k = 0; k < t.entries.size(); k++)
        {
            auto &e = t.entries[k];
            auto target = in.offsets.offset(e.target);
            std::string what = target ? label(*target) : cpu(e.target);
            line("- " + std::to_string(k) + ": " + what + (e.seen ? ", jumped through " + thousands(e.hits) + " times"
                                                                  : ", likely (not seen used)"));
        }
        line("");
    }

    // The RAM index.
    line("## RAM index");
    line("");
    line("Work RAM addresses named in the code, most functions first: the ones many functions read and few write "
         "are often state (a game mode, a counter, the player's position).");
    line("");
    std::vector<std::pair<uint32_t, const RamUse *>> rows;
    for (auto &[a, u] : ram)
        rows.push_back({ a, &u });
    std::stable_sort(rows.begin(), rows.end(), [](auto &a, auto &b) {
        size_t na = a.second->readers.size() + a.second->writers.size();
        size_t nb = b.second->readers.size() + b.second->writers.size();
        return na != nb ? na > nb : a.first < b.first;
    });
    for (size_t r = 0; r < rows.size() && r < in.max_ram; r++)
    {
        auto list = [&](const std::set<uint32_t> &s) {
            std::string out;
            size_t n = 0;
            for (auto e : s)
                if (n++ < 6)
                    out += (out.empty() ? "" : ", ") + label(e);
            if (s.size() > 6)
                out += ", +" + std::to_string(s.size() - 6);
            return out.empty() ? std::string("none") : out;
        };
        line("- " + cpu(rows[r].first) + ": read by " + std::to_string(rows[r].second->readers.size()) + " (" +
             list(rows[r].second->readers) + "); written by " + std::to_string(rows[r].second->writers.size()) + " (" +
             list(rows[r].second->writers) + ")");
    }
    return out;
}

} // namespace corrupt
