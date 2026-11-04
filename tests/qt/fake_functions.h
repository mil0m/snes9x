#pragma once
// A stand-in for the running game, for the Functions window and Change It… tests.
#include "FunctionSource.hpp"

#include <string>
#include <utility>
#include <vector>

using namespace corrupt;

struct FakeFunctions : FunctionSource
{
    std::vector<FunctionRow> rows;
    bool on = true;
    std::vector<uint32_t> flipped, forgotten;
    struct Kept
    {
        uint32_t entry;
        int hotkey;
        bool start_off;
        std::string note;
        uint32_t every = 0, for_frames = 0;
    };
    std::vector<Kept> kept;
    std::vector<std::pair<uint32_t, std::string>> named;
    struct Protected
    {
        uint32_t entry;
        bool on, deep;
    };
    std::vector<Protected> protects;

    FakeFunctions()
    {
        FunctionRow busy;
        busy.f = { .entry = 0x0019EE, .type = CallType::Near, .calls = 17972, .callers = 1, .frames = 17972, .now = 300 };
        busy.address = 0x8099EE;
        busy.body_bytes = 46;
        busy.runs = { { 0x0019EE, 0x001A0F }, { 0x001A40, 0x001A4C } };
        busy.label = "scroll (erik)";
        FunctionRow lng;
        lng.f = { .entry = 0x000142, .type = CallType::Long, .calls = 31043, .callers = 8, .frames = 17779, .now = 150 };
        lng.address = 0x808142;
        lng.off = true;
        Switch s;
        s.entry = 0x000142;
        s.ret = kRtl;
        s.hotkey = 3;
        s.note = "gravity?";
        lng.kept = s;
        lng.label = "gravity";
        lng.my_name = "gravity";
        lng.body_bytes = 12;
        lng.runs = { { 0x000142, 0x00014D } };
        lng.guarded = true;
        lng.protect = Protect{ 0x000142, true };
        FunctionRow mixed;
        mixed.f = { .entry = 0x002000, .type = CallType::Mixed, .calls = 10, .callers = 2, .frames = 5, .now = 0 };
        mixed.address = 0x80A000;
        rows = { busy, lng, mixed };
    }
    std::vector<FunctionRow> functions() override { return rows; }
    // A function the game just started calling, busier than any (the session sorts busiest first).
    void add_busy(uint32_t entry)
    {
        FunctionRow r;
        r.f = { .entry = entry, .type = CallType::Near, .calls = 1, .callers = 1, .frames = 1, .now = 1000 };
        r.address = 0x800000 | entry;
        rows.insert(rows.begin(), r);
    }
    bool running() override { return on; }
    std::string flip(uint32_t entry) override
    {
        flipped.push_back(entry);
        return {};
    }
    std::string keep(uint32_t entry, int hotkey, bool start_off, const std::string &note, uint32_t every,
                     uint32_t for_frames) override
    {
        kept.push_back({ entry, hotkey, start_off, note, every, for_frames });
        return {};
    }
    void forget(uint32_t entry) override { forgotten.push_back(entry); }

    // Change a function (#60): the busy one has a line of each kind; ROM bytes live in `now`.
    std::vector<uint8_t> clean = std::vector<uint8_t>(0x3000, 0xEA), now = clean;
    std::vector<std::pair<Change, bool>> applied; // the change, and on or off
    std::vector<Change> kept_changes;
    bool placed = false;
    std::vector<uint32_t> forgotten_changes;
    std::vector<ChangeOption> busy_options()
    {
        auto put = [&](uint32_t at, std::vector<uint8_t> bytes) { // once: later calls keep what was applied
            for (size_t i = 0; i < bytes.size() && !placed; i++)
                clean[at + i] = now[at + i] = bytes[i];
        };
        put(0x19EE, { 0xA9, 0x04 });       // LDA #$04
        put(0x19F0, { 0x69, 0x01 });       // ADC #$01
        put(0x19F2, { 0xF0, 0x02 });       // BEQ +2
        put(0x19F4, { 0xE6, 0x20 });       // INC $20
        put(0x19F6, { 0x20, 0x3C, 0x9D }); // JSR $9D3C
        placed = true;
        std::vector<ChangeOption> os(5);
        os[0] = { .at = 0x19EE, .address = 0x8099EE, .length = 2, .opcode = 0xA9, .kind = ChangeKind::Number, .text = "LDA #$04", .width = 1, .value = 4 };
        os[1] = { .at = 0x19F0, .address = 0x8099F0, .length = 2, .opcode = 0x69, .kind = ChangeKind::Number, .text = "ADC #$01", .width = 1, .value = 1, .other = 0xE9 };
        os[2] = { .at = 0x19F2, .address = 0x8099F2, .length = 2, .opcode = 0xF0, .kind = ChangeKind::Branch, .text = "BEQ $99F6" };
        os[3] = { .at = 0x19F4, .address = 0x8099F4, .length = 2, .opcode = 0xE6, .kind = ChangeKind::Math, .text = "INC $20", .other = 0xC6 };
        os[4] = { .at = 0x19F6, .address = 0x8099F6, .length = 3, .opcode = 0x20, .kind = ChangeKind::Skip, .text = "JSR $9D3C", .target = 0x1D3C, .call = CallType::Near };
        return os;
    }
    std::vector<ChangeOption> changeOptions(uint32_t entry) override
    {
        return entry == 0x0019EE ? busy_options() : std::vector<ChangeOption>{};
    }
    std::vector<FeedReading> changeFeeds(uint32_t entry) override
    {
        if (entry != 0x0019EE)
            return {};
        return { { Feed::Variable, "stored into $7E:0020" },
                 { Feed::Argument, "the sum is handed to a call (JSR $9D3C)" },
                 { Feed::StackPath, "the skipped block leaves the stack deeper: a later pull or return finds the wrong value" },
                 { Feed::Variable, "changes the memory directly: stored into $7E:0020" },
                 { Feed::Unknown, "can't tell whether the caller uses the result" } };
    }
    std::vector<uint8_t> romNow(uint32_t at, size_t n) override { return { now.begin() + at, now.begin() + at + n }; }
    std::vector<uint8_t> romClean(uint32_t at, size_t n) override { return { clean.begin() + at, clean.begin() + at + n }; }
    std::optional<std::vector<uint8_t>> redirectBytes(const ChangeOption &o, uint32_t entry) override
    {
        for (auto &r : rows)
            if (r.f.entry == entry && r.address && o.address && (*r.address >> 16) == (*o.address >> 16))
                return std::vector<uint8_t>{ 0x20, uint8_t(*r.address), uint8_t(*r.address >> 8) };
        return std::nullopt;
    }
    std::string applyChange(const Change &c, bool on_) override
    {
        if (!on)
            return "corruption is off";
        applied.push_back({ c, on_ });
        auto &bytes = on_ ? c.value : c.was;
        for (size_t i = 0; i < bytes.size(); i++)
            now[c.at + i] = bytes[i];
        return {};
    }
    std::string keepChange(const Change &c) override
    {
        kept_changes.push_back(c);
        for (auto &r : rows)
            if (r.f.entry == c.function)
            {
                std::erase_if(r.changes, [&](const Change &k) { return k.at == c.at; });
                r.changes.push_back(c);
            }
        return {};
    }
    void forgetChange(uint32_t at) override
    {
        forgotten_changes.push_back(at);
        for (auto &r : rows)
            std::erase_if(r.changes, [&](const Change &k) { return k.at == at; });
    }
    // Trying ahead (#78): asked for here, answered by finish_try (as the session does between frames).
    std::vector<Change> tries;
    std::optional<TriedChange> last;
    std::string tryChange(const Change &c) override
    {
        if (!on)
            return "corruption is off";
        tries.push_back(c);
        return {};
    }
    std::optional<TriedChange> lastTry() override { return last; }
    void finish_try(TryResult::Kind kind, const std::string &text)
    {
        TryResult r;
        r.kind = kind;
        r.text = text;
        last = TriedChange{ last ? last->id + 1 : 1, tries.back(), r };
    }
    std::string protect(uint32_t entry, bool on, bool deep) override
    {
        protects.push_back({ entry, on, deep });
        return {};
    }
    std::string setName(uint32_t entry, const std::string &name) override
    {
        named.push_back({ entry, name });
        for (auto &r : rows)
            if (r.f.entry == entry)
            {
                r.my_name = name;
                r.label = name;
            }
        return {};
    }
};
