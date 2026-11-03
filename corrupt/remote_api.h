#pragma once
#include "memory.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Remote control from a browser (roadmap #53): the protocol and its checks, without Qt.
// Plan: docs/corruption/2026-11-01-remote-plan.md. A remote may only do what's on the
// fixed list below; nothing touches files. The Qt side (RemoteServer) carries the
// messages and implements Control on the session.
namespace corrupt::remote
{

using Clock = std::chrono::steady_clock;

constexpr uint32_t kMaxWatch = 128 * 1024;  // bytes of memory a remote watches at once
constexpr size_t kMaxPaint = 4096;          // offsets in one paint
constexpr unsigned kPaintsPerSecond = 20;
constexpr int kMaxHotkey = 9;

// What a remote sees of a rule.
struct RuleView
{
    std::string name;
    std::string what; // the panel's words for it
    bool enabled = true;
    double strength = 1.0;
    int hotkey = 0; // 1-9 for a hotkey rule, else 0
    std::string effect; // what it does at that strength times all rules' (describe_strength)
};

// A button of the recipe (#63): slot 1-9, named after what it does.
struct ButtonView
{
    int key = 0;
    std::string label;
    std::string details;
    bool toggle = false;
    bool on = false;
};

// What a remote sees of the session.
struct StatusView
{
    bool game_loaded = false;
    std::string game;
    std::string recipe;
    bool active = false;
    bool starting = false;
    bool has_recipe = false;
    double chaos = 1.0;
    uint64_t seed = 0;
    unsigned crashes = 0;
    std::string message;
    std::vector<RuleView> rules;
    std::vector<ButtonView> buttons;
    std::array<size_t, kTargetCount> sizes{};
    std::vector<std::string> clients;  // who is connected
    std::vector<std::string> starters; // names of the recipes that come with the app
};
std::string status_json(const StatusView &s); // {"status":{...}}

// What a remote can do: the session behind it.
struct Control
{
    virtual std::string set_active(bool on) = 0; // error ("no recipe loaded") or empty
    virtual void back_to_normal() = 0;
    virtual void normal_here() = 0; // the same, but the game stays where it is (#70)
    virtual void reroll() = 0;
    virtual void mark_crash() = 0;
    virtual void set_chaos(double chaos) = 0;
    virtual void set_rule_enabled(size_t index, bool on) = 0;
    virtual void set_rule_strength(size_t index, double strength) = 0;
    virtual void press_hotkey(int n) = 0;
    virtual std::string paint(Target t, const std::vector<uint32_t> &offsets, uint8_t value) = 0; // error or empty
    // The recipes that come with the app (never files): load one by its place in the list.
    virtual size_t starter_count() = 0;
    virtual std::string starter_name(size_t index) = 0;
    virtual std::string load_starter(size_t index) = 0; // error or empty
    virtual size_t rule_count() = 0;
    virtual size_t memory_size(Target t) = 0;
    virtual ~Control() = default;
};

// The memory window a connection watches.
struct Watch
{
    Target target = Target::WRAM;
    uint32_t offset = 0;
    uint32_t length = 0;
    bool operator==(const Watch &) const = default;
};

// Wrong tokens across all connections: after kTries in a minute, nobody gets in for a minute.
class Gate
{
  public:
    static constexpr unsigned kTries = 5;
    bool open(Clock::time_point now);
    void failed(Clock::time_point now);

  private:
    std::deque<Clock::time_point> failures;
    std::optional<Clock::time_point> shut_until;
};

// One browser connection: the handshake, then actions.
class Connection
{
  public:
    Connection(Control &control, std::string token, Gate &gate) : control(control), token(std::move(token)), gate(gate)
    {
    }

    struct Reply
    {
        std::string text;        // to send back (JSON); may be empty
        bool close = false;      // and close the connection
        bool welcomed = false;   // the handshake just succeeded: send the status too
        bool watch_changed = false;
        std::string did;         // what happened, in words, for the panel ("pressed hotkey 1")
    };
    Reply handle(std::string_view message, Clock::time_point now);

    bool authorised() const { return ok; }
    const std::optional<Watch> &watch() const { return watching; }

  private:
    Control &control;
    std::string token;
    Gate &gate;
    bool ok = false;
    std::optional<Watch> watching;
    std::deque<Clock::time_point> paints;
};

// Equal without telling, by its timing, how much of a guess was right.
bool same_token(std::string_view a, std::string_view b);
// 32 random bytes from the system's source, base64url without padding.
std::string new_token();
// The 256 colours the viewer gives byte values, as "#rrggbb", for the welcome.
std::vector<std::string> colour_table();
// A memory window as a binary message: "S9XM", the memory (its index in kAllTargets),
// the offset (4 bytes, little-endian), the bytes.
std::vector<uint8_t> memory_message(Target t, uint32_t offset, std::span<const uint8_t> bytes);

} // namespace corrupt::remote
