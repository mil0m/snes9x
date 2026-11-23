#include "doctest/doctest.h"
#include "corrupt/script_engines.h"

#include <random>
#include <set>

using namespace corrupt;

namespace
{

// A fake interpreter: scripts in a fake ROM, a command table, and the dispatches it makes as the
// tracer would note them.
struct Interpreter
{
    std::vector<uint8_t> rom = std::vector<uint8_t>(0x40000, 0);
    std::map<uint8_t, int> lengths; // command -> its length
    uint32_t table = 0x8000;        // for a jump through RAM: the handler table's ROM offset
    uint16_t bias = 0x0100;         // for an indexed jump: X = stride x command + bias
    int stride = 2;
    bool through_ram = false;
    std::mt19937 rng{ 7 };
    int noise = 0; // unrelated ROM reads older than the command's

    uint32_t handler(uint8_t v) const { return 0xC10000 + v * 0x10; }

    // Writes a script at `at`: each command followed by its arguments.
    void script(uint32_t at, const std::vector<uint8_t> &commands)
    {
        for (uint8_t v : commands)
        {
            rom[at] = v;
            for (int i = 1; i < lengths.at(v); i++)
                rom[at + i] = uint8_t(rng());
            at += uint32_t(lengths.at(v));
        }
    }

    // The dispatch of the command at `at`, as noted.
    ScriptObservation dispatch(uint32_t at)
    {
        ScriptObservation o;
        uint8_t v = rom[at];
        o.target = handler(v);
        o.through_pointer = through_ram;
        std::vector<std::pair<uint32_t, uint8_t>> reads; // newest first
        if (through_ram)
        {
            uint32_t entry = table + uint32_t(stride) * v;
            reads.push_back({ entry, rom[entry] });
            o.index = entry;
            o.entry = 0;
        }
        else
            o.index = uint16_t(stride * v + bias);
        o.has_index = true;
        reads.push_back({ at, v });
        for (int i = 0; i < noise; i++)
        {
            uint32_t r = 0x20000 + rng() % 0x10000;
            reads.push_back({ r, rom[r] });
        }
        o.reads = uint8_t(std::min<size_t>(reads.size(), kScriptReads));
        for (int i = 0; i < o.reads; i++)
            o.offset[i] = reads[size_t(i)].first, o.value[i] = reads[size_t(i)].second;
        return o;
    }

    // Runs the script at `at` to its end (`count` commands), noting each dispatch at `site`.
    uint32_t run(ScriptLog &log, uint32_t site, uint32_t at, int count)
    {
        for (int i = 0; i < count; i++)
        {
            log.note(site, dispatch(at));
            at += uint32_t(lengths.at(rom[at]));
        }
        return at;
    }
};

const ScriptEngine *engine_at(const std::vector<ScriptEngine> &es, uint32_t site)
{
    for (auto &e : es)
        if (e.site == site)
            return &e;
    return nullptr;
}

} // namespace

TEST_CASE("an indexed interpreter is found with its commands and their lengths (#43)")
{
    Interpreter in;
    in.lengths = { { 0x10, 1 }, { 0x11, 2 }, { 0x12, 3 }, { 0x13, 4 } };
    in.noise = 3;
    std::vector<uint8_t> cmds;
    for (int i = 0; i < 40; i++)
        cmds.push_back(uint8_t(0x10 + (i * 7) % 4));
    in.script(0x10000, cmds);
    ScriptLog log;
    for (int pass = 0; pass < 5; pass++)
        in.run(log, 0x1234, 0x10000, 40);

    auto es = find_script_engines(log);
    REQUIRE(es.size() == 1);
    auto &e = es[0];
    CHECK(e.site == 0x1234);
    CHECK_FALSE(e.through_pointer);
    CHECK(e.stride == 2);
    CHECK(e.bias == 0x0100);
    CHECK(e.dispatches == 200);
    REQUIRE(e.commands.size() == 4);
    for (auto [v, len] : in.lengths)
    {
        CHECK(e.commands.at(v).length == len);
        CHECK(e.commands.at(v).handler == in.handler(v));
        CHECK(e.commands.at(v).usable());
    }
    CHECK(e.places.size() == 40);
    CHECK(e.first == 0x10000);
    CHECK(e.sequential > 0.9);
    // Every place but the script's last (whose next command was elsewhere) is steady.
    CHECK(e.usable_places().size() == 39);
    CHECK(e.same_length(3) == std::vector<uint8_t>{ 0x12 });
}

TEST_CASE("a table lookup from one place isn't a script engine")
{
    // The same ROM byte read every time, then an indexed jump by it: one place.
    ScriptLog log;
    for (int i = 0; i < 100; i++)
    {
        ScriptObservation o;
        o.has_index = true;
        o.reads = 1;
        o.offset[0] = 0x5000;
        o.value[0] = uint8_t(i % 5);
        o.index = uint16_t(2 * o.value[0]);
        o.target = 0xC00000 + o.value[0];
        log.note(0x2000, o);
    }
    CHECK(find_script_engines(log).empty());
}

TEST_CASE("a stride-3 table isn't locked out by an ambiguous start")
{
    Interpreter in;
    in.stride = 3;
    in.lengths = { { 0x02, 2 }, { 0x04, 2 }, { 0x05, 1 }, { 0x07, 3 }, { 0x09, 2 } };
    // A title loop running two commands, where other strides fit as well...
    in.script(0x10000, { 0x02, 0x04, 0x02, 0x04 });
    // ...then the real scripts.
    std::vector<uint8_t> cmds;
    for (int i = 0; i < 60; i++)
        cmds.push_back(std::vector<uint8_t>{ 0x02, 0x04, 0x05, 0x07, 0x09 }[size_t(i * 3) % 5]);
    in.script(0x11000, cmds);
    ScriptLog log;
    for (int i = 0; i < 30; i++)
        in.run(log, 0x3000, 0x10000, 4);
    for (int i = 0; i < 3; i++)
        in.run(log, 0x3000, 0x11000, 60);

    auto es = find_script_engines(log);
    REQUIRE(es.size() == 1);
    CHECK(es[0].stride == 3);
    CHECK(es[0].bias == 0x0100);
    CHECK(es[0].commands.size() == 5);
}

TEST_CASE("a shared trampoline carries two engines, and plain dispatches don't count")
{
    Interpreter events, actions;
    events.through_ram = actions.through_ram = true;
    events.table = 0x8000;
    actions.table = 0x9000;
    events.lengths = { { 0x35, 2 }, { 0x36, 3 }, { 0x40, 1 }, { 0x41, 2 }, { 0x48, 3 }, { 0x6B, 6 } };
    actions.lengths = { { 0xC0, 1 }, { 0xC1, 1 }, { 0xC5, 2 }, { 0xC8, 3 }, { 0xCC, 1 }, { 0xD0, 2 } };
    actions.rom = events.rom; // one ROM
    std::vector<uint8_t> e_cmds, a_cmds;
    for (int i = 0; i < 50; i++)
    {
        e_cmds.push_back(std::vector<uint8_t>{ 0x35, 0x36, 0x40, 0x41, 0x48, 0x6B }[size_t(i * 5) % 6]);
        a_cmds.push_back(std::vector<uint8_t>{ 0xC0, 0xC1, 0xC5, 0xC8, 0xCC, 0xD0 }[size_t(i * 5) % 6]);
    }
    events.script(0x10000, e_cmds);
    actions.rom = events.rom;
    actions.script(0x18000, a_cmds);
    events.rom = actions.rom;
    ScriptLog log;
    uint32_t e_at = 0x10000, a_at = 0x18000;
    for (int round = 0; round < 10; round++)
    {
        e_at = events.run(log, 0x0700, e_at, 5);
        a_at = actions.run(log, 0x0700, a_at, 5);
        // Another dispatcher through the same pointer, whose target no read explains.
        for (int i = 0; i < 20; i++)
        {
            ScriptObservation o;
            o.through_pointer = true;
            o.target = 0xC20000 + uint32_t(i);
            log.note(0x0700, o);
        }
    }
    auto es = find_script_engines(log);
    REQUIRE(es.size() == 2);
    std::set<uint32_t> tables = { es[0].bias, es[1].bias };
    CHECK(tables.count(0x8000));
    CHECK(tables.count(0x9000));
    for (auto &e : es)
    {
        CHECK(e.site == 0x0700);
        CHECK(e.through_pointer);
        CHECK(e.site_dispatches == 300);
        CHECK(e.commands.size() == 6);
        auto &lengths = e.bias == 0x8000 ? events.lengths : actions.lengths;
        for (auto &[v, c] : e.commands)
            CHECK(c.length == lengths.at(v));
    }
}

TEST_CASE("a branch is branchy: never picked, never put in")
{
    Interpreter in;
    in.lengths = { { 0x01, 1 }, { 0x02, 2 }, { 0x03, 3 }, { 0x04, 3 }, { 0x20, 3 } };
    // 0x20 is "if, else go elsewhere": taken half the time, to a far place.
    std::vector<uint8_t> cmds;
    for (int i = 0; i < 40; i++)
        cmds.push_back(std::vector<uint8_t>{ 0x01, 0x02, 0x20, 0x03, 0x04 }[size_t(i) % 5]);
    in.script(0x10000, cmds);
    in.script(0x30000, { 0x01, 0x02, 0x03, 0x04, 0x01 });
    ScriptLog log;
    for (int pass = 0; pass < 6; pass++)
    {
        uint32_t at = 0x10000;
        for (int i = 0; i < 40; i++)
        {
            log.note(0x4000, in.dispatch(at));
            uint8_t v = in.rom[at];
            if (v == 0x20 && (i + pass) % 2)
            {
                in.run(log, 0x4000, 0x30000, 5); // taken: runs elsewhere, then comes back after it
                at += 3;
                continue;
            }
            at += uint32_t(in.lengths.at(v));
        }
    }
    auto es = find_script_engines(log);
    REQUIRE(es.size() == 1);
    auto &e = es[0];
    CHECK(e.commands.at(0x20).branchy);
    CHECK_FALSE(e.commands.at(0x20).usable());
    CHECK_FALSE(e.commands.at(0x03).branchy);
    for (auto &p : e.usable_places())
        CHECK(p.value != 0x20);
    CHECK(e.same_length(3) == std::vector<uint8_t>{ 0x03, 0x04 });
}

TEST_CASE("waits run again at one place don't spoil a command's length")
{
    Interpreter in;
    in.lengths = { { 0x91, 1 }, { 0x35, 2 }, { 0x48, 3 } };
    std::vector<uint8_t> cmds;
    for (int i = 0; i < 30; i++)
        cmds.push_back(std::vector<uint8_t>{ 0x35, 0x91, 0x48 }[size_t(i) % 3]);
    in.script(0x10000, cmds);
    ScriptLog log;
    for (int pass = 0; pass < 4; pass++)
    {
        uint32_t at = 0x10000;
        for (int i = 0; i < 30; i++)
        {
            int repeats = in.rom[at] == 0x91 ? 6 : 1; // a wait: dispatched each frame until done
            for (int r = 0; r < repeats; r++)
                log.note(0x5000, in.dispatch(at));
            at += uint32_t(in.lengths.at(in.rom[at]));
        }
    }
    auto es = find_script_engines(log);
    REQUIRE(es.size() == 1);
    CHECK(es[0].commands.at(0x91).length == 1);
    CHECK(es[0].sequential > 0.9);
}

TEST_CASE("coincidences don't push an engine out of a busy site")
{
    Interpreter in;
    in.noise = 14; // nearly a full ring of unrelated reads every time
    in.through_ram = true;
    in.lengths = { { 0x10, 1 }, { 0x11, 2 }, { 0x12, 2 }, { 0x13, 3 }, { 0x14, 1 }, { 0x15, 4 } };
    std::mt19937 rng(3);
    for (uint32_t i = 0x20000; i < 0x30000; i++)
        in.rom[i] = uint8_t(rng());
    std::vector<uint8_t> cmds;
    for (int i = 0; i < 80; i++)
        cmds.push_back(uint8_t(0x10 + (i * 5) % 6));
    in.script(0x10000, cmds);
    ScriptLog log;
    uint32_t at = 0x10000;
    int ran = 0;
    for (int round = 0; round < 400; round++)
    {
        // Ten unrelated dispatches with random-looking reads for every command the engine runs.
        for (int i = 0; i < 10; i++)
        {
            ScriptObservation o;
            o.through_pointer = true;
            o.has_index = true;
            o.entry = 0;
            o.reads = kScriptReads;
            for (int k = 0; k < kScriptReads; k++)
                o.offset[k] = 0x20000 + rng() % 0x10000, o.value[k] = in.rom[o.offset[k]];
            o.index = o.offset[0];
            o.target = 0xC30000 + rng() % 64;
            log.note(0x0700, o);
        }
        log.note(0x0700, in.dispatch(at));
        at += uint32_t(in.lengths.at(in.rom[at]));
        if (++ran == 80)
            at = 0x10000, ran = 0;
    }
    CHECK(log.all().at(0x0700).candidates.size() < ScriptLog::kPrune);
    auto es = find_script_engines(log);
    REQUIRE(es.size() == 1);
    CHECK(es[0].bias == in.table);
    CHECK(es[0].commands.size() == 6);
    // A brand-new rule can be pruned among the crowd before its second dispatch: a few are lost.
    CHECK(es[0].dispatches >= 395);
}

TEST_CASE("two logs merged find what one log of both would")
{
    Interpreter in;
    in.lengths = { { 0x10, 1 }, { 0x11, 2 }, { 0x12, 3 }, { 0x13, 2 } };
    std::vector<uint8_t> cmds;
    for (int i = 0; i < 30; i++)
        cmds.push_back(uint8_t(0x10 + (i * 3) % 4));
    in.script(0x10000, cmds);
    ScriptLog one, mine, theirs;
    for (int pass = 0; pass < 6; pass++)
    {
        in.run(one, 0x1234, 0x10000, 30);
        in.run(pass < 3 ? mine : theirs, 0x1234, 0x10000, 30);
    }
    mine.merge(theirs);
    auto a = find_script_engines(one), b = find_script_engines(mine);
    REQUIRE(a.size() == 1);
    REQUIRE(b.size() == 1);
    CHECK(a[0].dispatches == b[0].dispatches);
    CHECK(a[0].places.size() == b[0].places.size());
    for (auto &[v, c] : a[0].commands)
    {
        CHECK(b[0].commands.at(v).runs == c.runs);
        CHECK(b[0].commands.at(v).length == c.length);
    }
}

TEST_CASE("the script engine file round-trips and refuses another game")
{
    Interpreter in;
    in.through_ram = true;
    in.noise = 4;
    for (uint32_t i = 0x20000; i < 0x30000; i++)
        in.rom[i] = uint8_t(i * 37);
    in.lengths = { { 0x35, 2 }, { 0x36, 3 }, { 0x40, 1 }, { 0x41, 2 }, { 0x48, 3 } };
    std::vector<uint8_t> cmds;
    for (int i = 0; i < 30; i++)
        cmds.push_back(std::vector<uint8_t>{ 0x35, 0x36, 0x40, 0x41, 0x48 }[size_t(i * 2) % 5]);
    in.script(0x10000, cmds);
    ScriptLog log;
    for (int pass = 0; pass < 4; pass++)
        in.run(log, 0x0700, 0x10000, 30);
    const size_t size = in.rom.size();
    std::string file = encode_script_log(log, size, 0xDEADBEEF);
    CHECK(file.find("\"snes9x-scripts\"") != std::string::npos);

    std::string error;
    auto back = decode_script_log(file, size, 0xDEADBEEF, error);
    REQUIRE(back);
    CHECK(error.empty());
    auto a = find_script_engines(log), b = find_script_engines(*back);
    REQUIRE(a.size() == 1);
    REQUIRE(b.size() == 1);
    CHECK(b[0].bias == a[0].bias);
    CHECK(b[0].through_pointer);
    CHECK(b[0].places.size() == a[0].places.size());
    for (auto &[v, c] : a[0].commands)
    {
        CHECK(b[0].commands.at(v).length == c.length);
        CHECK(b[0].commands.at(v).handler == c.handler);
    }
    // Coincidental candidates (fewer than 20 dispatches) aren't written.
    CHECK(back->all().at(0x0700).candidates.size() < log.all().at(0x0700).candidates.size());

    CHECK_FALSE(decode_script_log(file, size, 0x12345678, error));
    CHECK(error == "recorded for another version of the game");
    CHECK_FALSE(decode_script_log(file, size * 2, 0xDEADBEEF, error));
    CHECK(error == "recorded for another ROM size");
    CHECK_FALSE(decode_script_log("{\"format\": \"snes9x-jumps\"}", size, 0xDEADBEEF, error));
    CHECK_FALSE(decode_script_log("not json", size, 0xDEADBEEF, error));
}
