#pragma once
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The call graph (roadmap #26): the calls a game makes, recorded by the code map's tracer,
// and the functions behind them: how each is called (near or long) and how often it runs.
// Kill switches (#34) turn one off with a return at its entry.
// Plan: docs/corruption/2026-11-01-functions-plan.md.
namespace corrupt
{

// The three calls the tracer notes.
namespace callop
{
constexpr uint8_t Jsr = 0x20;        // JSR $abs: near, returns with RTS
constexpr uint8_t Jsl = 0x22;        // JSL $long: long, returns with RTL
constexpr uint8_t JsrIndexed = 0xFC; // JSR ($abs,X): near, through a table
constexpr bool noted(uint8_t op) { return op == Jsr || op == Jsl || op == JsrIndexed; }
} // namespace callop

constexpr uint8_t kRts = 0x60;
constexpr uint8_t kRtl = 0x6B;

enum class CallType : uint8_t
{
    Near = 1,
    Long = 2,
    Mixed = 3, // reached both ways: which return it needs isn't known
};
constexpr CallType call_type_of(uint8_t op) { return op == callop::Jsl ? CallType::Long : CallType::Near; }
// The byte that skips a function called this way; 0 for mixed.
constexpr uint8_t return_byte(CallType t) { return t == CallType::Near ? kRts : t == CallType::Long ? kRtl : 0; }

// One call site as it ran: the call instruction and the function's entry, as ROM offsets.
struct CallKey
{
    uint32_t site = 0;
    uint32_t target = 0;
    bool operator==(const CallKey &) const = default;
};

struct CallKeyHash
{
    size_t operator()(const CallKey &k) const
    {
        return size_t(((uint64_t(k.site) << 32) | k.target) * 0x9E3779B97F4A7C15ULL);
    }
};

struct CallSeen
{
    uint8_t opcode = 0;
    uint32_t hits = 0;
    uint64_t frame = 0; // the last frame it was noted in (not saved)
};

// Every call seen, with how often, and in how many frames each entry was called. Lives on
// the emulation thread; the tracer notes into it while frames run. Only ever grows, so logs
// from several people and sessions combine by adding them up.
class CallLog
{
  public:
    // A call is about to run. Cheap: one lookup; an entry's first call in a frame is also
    // queued for the frame count.
    void note(uint32_t site, uint8_t opcode, uint32_t target)
    {
        auto &seen = calls[CallKey{ site, target }];
        if (!seen.hits++)
            seen.opcode = opcode;
        if (seen.frame != now)
        {
            seen.frame = now;
            fresh.push_back(target);
        }
    }

    // A frame is over: each entry called in it counts one frame more. Returns the entries
    // called in it (each once, in order), for the keeper's "now".
    std::vector<uint32_t> end_frame();

    void add(const CallKey &key, uint8_t opcode, uint32_t hits); // hits added up
    void add_frames(uint32_t entry, uint64_t n) { frames[entry] += n; }
    void merge(const CallLog &other);
    void clear();

    size_t size() const { return calls.size(); } // distinct call sites
    bool empty() const { return calls.empty(); }
    const std::unordered_map<CallKey, CallSeen, CallKeyHash> &all() const { return calls; }
    const std::unordered_map<uint32_t, uint64_t> &frames_run() const { return frames; }
    // Grows when a new call site is seen; hits alone don't change it.
    uint64_t fingerprint() const { return calls.size(); }

  private:
    std::unordered_map<CallKey, CallSeen, CallKeyHash> calls;
    std::unordered_map<uint32_t, uint64_t> frames; // entry -> frames it was called in
    std::vector<uint32_t> fresh;                   // entries first called this frame (repeats possible)
    uint64_t now = 1;
};

// The file: JSON, {"format": "snes9x-calls", "version": 1, "rom_size": N, "calls": [{"site":
// "0x0081A3", "op": "20", "target": "0x009000", "hits": 1234}], "functions": [{"entry":
// "0x009000", "frames": 5120}]}, in offset order, one call a line, so a person's file diffs
// well in the shared library.
std::string encode_call_log(const CallLog &log, size_t rom_size);
std::optional<CallLog> decode_call_log(std::string_view file, size_t rom_size, std::string &error);

// A function: an entry something called, how, and how much it runs.
struct Function
{
    uint32_t entry = 0;
    CallType type = CallType::Near;
    uint32_t calls = 0;   // in all
    uint32_t callers = 0; // distinct call sites
    uint64_t frames = 0;  // frames it was called in, in all
    uint32_t now = 0;     // of the last kNowFrames frames (this session only)
    uint32_t first_site = 0; // the call site seen first in offset order, for showing where it's called from
};

// A game's call logs: this person's (the only one they write) and everyone else's, merged.
struct CallLogs
{
    CallLog mine;
    CallLog others;
    std::vector<std::string> errors; // files that couldn't be read or are for another ROM size
};

// The loaded game's call log while playing, like JumpLogKeeper: the tracer notes into
// mine(), end_frame() after every frame, saved once a minute while new calls turn up.
// Emulation thread, between frames.
class CallKeeper
{
  public:
    static constexpr uint64_t kSaveEveryFrames = 3600;
    static constexpr uint32_t kNowFrames = 300; // "now": the last 5 seconds

    void reset(CallLogs loaded = {});
    void end_frame(); // counts the frame's calls; also counts towards the next save
    CallLog &mine() { return own; }
    const CallLog &mine() const { return own; }
    CallLog combined() const;
    // Everyone's functions, busiest now first, then by frames run, then by entry.
    std::vector<Function> functions() const;
    bool save_due(); // once a minute, when mine has new calls; starts the minute over
    bool unsaved() const { return own.fingerprint() != saved_print; }
    void saved() { saved_print = own.fingerprint(); }
    // The run's frames were thrown away (a rollback, a run ahead): "now" starts over.
    void forget_now();

  private:
    CallLog own, others;
    uint64_t saved_print = 0;
    uint64_t frames = 0;
    std::deque<std::vector<uint32_t>> recent; // entries called, per frame, the last kNowFrames
    std::unordered_map<uint32_t, uint32_t> now; // entry -> frames of `recent` it was called in
};

// Functions sorted for picking: the `limit` that ran in the most frames (all of them for 0),
// skipping mixed ones, then in entry order, so a seed picks the same ones.
std::vector<Function> busiest(std::vector<Function> functions, size_t limit);

} // namespace corrupt
