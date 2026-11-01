#include "doctest/doctest.h"
#include "corrupt/byte_colour.h"
#include "corrupt/remote_api.h"
#include "nlohmann/json.hpp"

#include <set>

using namespace corrupt;
using namespace corrupt::remote;
using json = nlohmann::json;

namespace
{

constexpr const char *kToken = "s3cret-token";

// Records what the remote asked for.
struct FakeControl : Control
{
    std::vector<std::string> calls;
    size_t rules = 3;
    std::string paint_error;
    std::vector<uint32_t> painted;
    uint8_t painted_value = 0;
    Target painted_target = Target::WRAM;

    std::string active_error;
    std::string set_active(bool on) override
    {
        calls.push_back(on ? "active on" : "active off");
        return active_error;
    }
    void back_to_normal() override { calls.push_back("back"); }
    void reroll() override { calls.push_back("reroll"); }
    void mark_crash() override { calls.push_back("crash"); }
    void set_chaos(double c) override { calls.push_back("chaos " + std::to_string(int(c * 100))); }
    void set_rule_enabled(size_t i, bool on) override
    {
        calls.push_back("rule " + std::to_string(i) + (on ? " on" : " off"));
    }
    void set_rule_strength(size_t i, double s) override
    {
        calls.push_back("strength " + std::to_string(i) + " " + std::to_string(int(s * 100)));
    }
    void press_hotkey(int n) override { calls.push_back("hotkey " + std::to_string(n)); }
    std::string paint(Target t, const std::vector<uint32_t> &offsets, uint8_t value) override
    {
        calls.push_back("paint");
        painted_target = t;
        painted = offsets;
        painted_value = value;
        return paint_error;
    }
    size_t rule_count() override { return rules; }
    size_t memory_size(Target t) override { return t == Target::WRAM ? 0x20000 : t == Target::ROM ? 0x100000 : 0x800; }
};

struct Fixture
{
    FakeControl control;
    Gate gate;
    Connection conn{ control, kToken, gate };
    Clock::time_point now = Clock::time_point{} + std::chrono::hours(1);

    Connection::Reply send(const json &j) { return conn.handle(j.dump(), now); }
    void hello() { REQUIRE(send({ { "hello", 1 }, { "token", kToken } }).welcomed); }
    // An action; returns the reply as JSON.
    json act(json j, int id = 7)
    {
        j["id"] = id;
        auto r = send(j);
        CHECK(!r.close);
        return json::parse(r.text);
    }
};

} // namespace

TEST_SUITE("remote control")
{
    TEST_CASE("the first message must be hello with the right token")
    {
        Fixture f;
        auto r = f.send({ { "do", "reroll" } });
        CHECK(r.close);
        CHECK(f.control.calls.empty());
        CHECK(!f.conn.authorised());

        Fixture g;
        r = g.send({ { "hello", 1 }, { "token", "wrong" } });
        CHECK(r.close);
        CHECK(!g.conn.authorised());

        Fixture h;
        r = h.send({ { "hello", 1 }, { "token", kToken } });
        CHECK(!r.close);
        CHECK(r.welcomed);
        CHECK(h.conn.authorised());
        auto j = json::parse(r.text);
        CHECK(j["welcome"] == 1);
        REQUIRE(j["colours"].size() == 256);
        CHECK(j["colours"][0].get<std::string>().size() == 7);
    }

    TEST_CASE("not JSON, or not an object: closed")
    {
        Fixture f;
        CHECK(f.conn.handle("{nope", f.now).close);
        Fixture g;
        CHECK(g.conn.handle("[1,2]", g.now).close);
    }

    TEST_CASE("five wrong tokens in a minute shut the gate for everyone for a minute")
    {
        FakeControl control;
        Gate gate;
        auto t = Clock::time_point{} + std::chrono::hours(1);
        for (int i = 0; i < 5; i++)
        {
            Connection c(control, kToken, gate);
            CHECK(c.handle(json{ { "hello", 1 }, { "token", "guess" + std::to_string(i) } }.dump(), t).close);
            t += std::chrono::seconds(1);
        }
        // Even the right token is refused now.
        Connection right(control, kToken, gate);
        auto r = right.handle(json{ { "hello", 1 }, { "token", kToken } }.dump(), t);
        CHECK(r.close);
        CHECK(json::parse(r.text)["error"].get<std::string>().find("later") != std::string::npos);
        t += std::chrono::seconds(61);
        Connection later(control, kToken, gate);
        CHECK(later.handle(json{ { "hello", 1 }, { "token", kToken } }.dump(), t).welcomed);
    }

    TEST_CASE("wrong tokens spread out don't shut the gate")
    {
        FakeControl control;
        Gate gate;
        auto t = Clock::time_point{} + std::chrono::hours(1);
        for (int i = 0; i < 10; i++)
        {
            Connection c(control, kToken, gate);
            c.handle(json{ { "hello", 1 }, { "token", "x" } }.dump(), t);
            t += std::chrono::seconds(20);
        }
        Connection right(control, kToken, gate);
        CHECK(right.handle(json{ { "hello", 1 }, { "token", kToken } }.dump(), t).welcomed);
    }

    TEST_CASE("every action on the list reaches the session, and says what it did")
    {
        Fixture f;
        f.hello();
        CHECK(f.act({ { "do", "active" }, { "on", true } })["done"] == 7);
        f.act({ { "do", "active" }, { "on", false } });
        f.act({ { "do", "back_to_normal" } });
        f.act({ { "do", "reroll" } });
        f.act({ { "do", "mark_crash" } });
        f.act({ { "do", "chaos" }, { "value", 0.25 } });
        f.act({ { "do", "rule_enabled" }, { "rule", 2 }, { "on", false } });
        f.act({ { "do", "rule_strength" }, { "rule", 1 }, { "value", 0.5 } });
        auto r = f.send({ { "do", "hotkey" }, { "key", 3 }, { "id", 1 } });
        CHECK(r.did == "pressed hotkey 3");
        CHECK(f.control.calls == std::vector<std::string>{ "active on", "active off", "back", "reroll", "crash",
                                                            "chaos 25", "rule 2 off", "strength 1 50", "hotkey 3" });
    }

    TEST_CASE("anything off the list, or out of range, is refused and changes nothing")
    {
        Fixture f;
        f.hello();
        for (json bad : std::vector<json>{
                 { { "do", "load_recipe" }, { "path", "/etc/passwd" } },
                 { { "do", "export_rom" } },
                 { { "nothing", 1 } },
                 { { "do", 5 } },
                 { { "do", "active" } },                                       // no on
                 { { "do", "active" }, { "on", "yes" } },                      // not a bool
                 { { "do", "chaos" }, { "value", 1.5 } },                      // over 1
                 { { "do", "chaos" }, { "value", -0.1 } },                     // under 0
                 { { "do", "chaos" }, { "value", "high" } },                   // not a number
                 { { "do", "rule_enabled" }, { "rule", 3 }, { "on", true } },  // only 3 rules
                 { { "do", "rule_enabled" }, { "rule", -1 }, { "on", true } }, // negative
                 { { "do", "rule_strength" }, { "rule", 0 }, { "value", 2 } }, // over 1
                 { { "do", "hotkey" }, { "key", 0 } },
                 { { "do", "hotkey" }, { "key", 10 } },
                 { { "do", "watch" }, { "memory", "disk" }, { "offset", 0 }, { "length", 16 } },
                 { { "do", "watch" }, { "memory", "wram" }, { "offset", 0x1FFF0 }, { "length", 0x20 } }, // past the end
                 { { "do", "watch" }, { "memory", "wram" }, { "offset", 0 }, { "length", 0x20001 } },    // too big
                 { { "do", "watch" }, { "memory", "wram" }, { "offset", 0 }, { "length", 0 } },
                 { { "do", "paint" }, { "memory", "wram" }, { "offsets", { 0x20000 } }, { "value", 1 } }, // outside
                 { { "do", "paint" }, { "memory", "wram" }, { "offsets", { 1 } }, { "value", 256 } },
                 { { "do", "paint" }, { "memory", "wram" }, { "offsets", "1" }, { "value", 1 } },
                 { { "do", "paint" }, { "memory", "wram" }, { "offsets", json::array() }, { "value", 1 } },
             })
        {
            CAPTURE(bad.dump());
            auto j = f.act(bad);
            CHECK(j.contains("error"));
            CHECK(j["id"] == 7);
        }
        CHECK(f.control.calls.empty());
        CHECK(!f.conn.watch());
    }

    TEST_CASE("the session's own refusal comes back as the error")
    {
        Fixture f;
        f.hello();
        f.control.active_error = "No recipe loaded";
        auto j = f.act({ { "do", "active" }, { "on", true } });
        CHECK(j["error"] == "No recipe loaded");
    }

    TEST_CASE("a second hello after the first is refused")
    {
        Fixture f;
        f.hello();
        auto j = f.act({ { "hello", 1 }, { "token", kToken } });
        CHECK(j.contains("error"));
    }

    TEST_CASE("watch: a window inside the memory, and stopping")
    {
        Fixture f;
        f.hello();
        auto r = f.send({ { "do", "watch" }, { "memory", "rom" }, { "offset", 0x8000 }, { "length", 0x10000 }, { "id", 2 } });
        CHECK(r.watch_changed);
        REQUIRE(f.conn.watch());
        CHECK(*f.conn.watch() == Watch{ Target::ROM, 0x8000, 0x10000 });
        r = f.send({ { "do", "watch" }, { "memory", "none" }, { "id", 3 } });
        CHECK(r.watch_changed);
        CHECK(!f.conn.watch());
    }

    TEST_CASE("paint: the offsets and value reach the session; too many, or too often, are refused")
    {
        Fixture f;
        f.hello();
        auto j = f.act({ { "do", "paint" }, { "memory", "vram" }, { "offsets", { 1, 2, 3 } }, { "value", 0x42 } });
        CHECK(j["done"] == 7);
        CHECK(f.control.painted_target == Target::VRAM);
        CHECK(f.control.painted == std::vector<uint32_t>{ 1, 2, 3 });
        CHECK(f.control.painted_value == 0x42);

        std::vector<uint32_t> many(kMaxPaint + 1, 5);
        CHECK(f.act({ { "do", "paint" }, { "memory", "wram" }, { "offsets", many }, { "value", 1 } }).contains("error"));

        // The session's own refusal comes back as the error.
        f.control.paint_error = "no game";
        CHECK(f.act({ { "do", "paint" }, { "memory", "wram" }, { "offsets", { 1 } }, { "value", 1 } })["error"] ==
              "no game");
        f.control.paint_error.clear();

        // 20 a second at most (two already went through in this second).
        int refused = 0;
        for (int i = 0; i < 30; i++)
            refused += f.act({ { "do", "paint" }, { "memory", "wram" }, { "offsets", { 1 } }, { "value", 1 } })
                           .contains("error");
        CHECK(refused == 12);
        f.now += std::chrono::seconds(1);
        CHECK(f.act({ { "do", "paint" }, { "memory", "wram" }, { "offsets", { 1 } }, { "value", 1 } })["done"] == 7);
    }

    TEST_CASE("the status as JSON")
    {
        StatusView s;
        s.game_loaded = true;
        s.game = "GOOF TROOP";
        s.recipe = "Code drip";
        s.active = true;
        s.chaos = 0.5;
        s.seed = 5101;
        s.crashes = 2;
        s.message = "Caught at $80:1234";
        s.rules = { { "drip", "rom: random", true, 1.0, 0 }, { "swap", "pointer swap", false, 0.5, 1 } };
        s.sizes[size_t(Target::WRAM)] = 0x20000;
        s.clients = { "erik@example.com" };
        auto j = json::parse(status_json(s))["status"];
        CHECK(j["game"] == "GOOF TROOP");
        CHECK(j["active"] == true);
        CHECK(j["chaos"] == 0.5);
        CHECK(j["seed"] == "5101"); // as text: JavaScript numbers lose 64-bit seeds
        CHECK(j["rules"].size() == 2);
        CHECK(j["rules"][1]["hotkey"] == 1);
        CHECK(j["rules"][1]["enabled"] == false);
        CHECK(j["sizes"]["wram"] == 0x20000);
        CHECK(j["clients"][0] == "erik@example.com");
    }

    TEST_CASE("tokens: random, long, URL-safe, and compared whole")
    {
        std::set<std::string> seen;
        for (int i = 0; i < 20; i++)
        {
            auto t = new_token();
            CHECK(t.size() == 43);
            CHECK(t.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") ==
                  std::string::npos);
            seen.insert(t);
        }
        CHECK(seen.size() == 20);
        CHECK(same_token("abc", "abc"));
        CHECK(!same_token("abc", "abd"));
        CHECK(!same_token("abc", "abcd"));
        CHECK(!same_token("", "a"));
        CHECK(!same_token("", "")); // no token set means nobody gets in
    }

    TEST_CASE("the colour table is the viewer's")
    {
        auto table = colour_table();
        REQUIRE(table.size() == 256);
        auto c = byte_colour(0x80);
        char want[8];
        snprintf(want, sizeof want, "#%02x%02x%02x", c.r, c.g, c.b);
        CHECK(table[0x80] == want);
    }

    TEST_CASE("a memory window as a binary message")
    {
        std::vector<uint8_t> bytes = { 9, 8, 7 };
        auto m = memory_message(Target::VRAM, 0x01020304, bytes);
        REQUIRE(m.size() == 4 + 1 + 4 + 3);
        CHECK(std::string(m.begin(), m.begin() + 4) == "S9XM");
        CHECK(m[4] == 2); // VRAM's index in kAllTargets
        CHECK(m[5] == 0x04);
        CHECK(m[8] == 0x01);
        CHECK(m[9] == 9);
        CHECK(m[11] == 7);
    }
}
