#pragma once
#include "crash_map.h"
#include "function_body.h"
#include "function_change.h"

#include <cstdint>
#include <span>
#include <string>

// What a function change feeds into (follow-up to #60): the same edit is harmless as a speed
// and deadly as an index, so the risk is in where the changed value goes next, not in the
// kind of change. Read from the code that follows the change, straight-line, in its body.
// First a measurement (corrupt-smoke --risk); the UI comes if it separates survivors.
namespace corrupt
{

enum class Feed : uint8_t
{
    Variable,     // stored into RAM and only used as a value (a speed, a timer)
    Scratch,      // stored into a temporary byte ($00-$1F) that code soon uses for something else
    Hardware,     // stored into a hardware register (not a DMA address or size: those are pointers)
    Index,        // becomes an index (TAX/TAY, ,X / ,Y, a jump table)
    LoopCount,    // counts a loop (DEX/INX then a branch back)
    LoopBound,    // compared, then a branch back: a loop's end
    Condition,    // compared or tested, then a forward branch
    Pointer,      // stored where code reads it as a pointer, or into D / S
    Argument,     // handed to a call, pushed, or returned
    Overwritten,  // replaced before anything uses it
    LoopBranch,   // a branch back, both ways ran: a loop's exit or continue
    ForwardBranch,// a branch forward, both ways ran: skips or takes a block
    UnseenPath,   // a branch that went only one way while the tracer watched: flipped, the other way runs
    WaitLoop,     // a branch back over a loop that only reads: flipped, it can wait forever
    WidthPath,    // the block it skips or loops over changes the register width (REP/SEP) for the join
    StackPath,    // the block it skips or loops over leaves the stack deeper or shallower
    ResultUsed,   // a skipped call whose result (A, X, Y, flags) the caller reads straight after
    ResultUnused, // a skipped call whose caller sets what it needs again
    Unknown,      // the trail goes cold (a branch, a jump, or too far)
};

const char *feed_name(Feed f); // "variable", "index"...

// How often a change of that kind stopped the game, as measured (corrupt-smoke --risk on Goof
// Troop, Pocky & Rocky and FE4, one change at a time, 30 s each): Low under 10%, Medium
// 10-25%, High above; Unclear when the trail went cold.
enum class Risk : uint8_t
{
    Low,
    Medium,
    High,
    Unclear,
};
Risk risk_of(Feed f);
const char *risk_name(Risk r); // "low", "medium", "high", "unclear"
// The measured share of such changes that stopped the game, in percent; -1 when too few were
// measured to say.
int measured_failures(Feed f);

// The whole function counts too: changes in a game's 16 busiest functions (its main loop and
// what runs every frame) stopped it 18% of the time, against 8% further down the list
// (corrupt-smoke --risk, 952 changes on Goof Troop, Pocky & Rocky and FE4).
constexpr size_t kBusiestFragile = 16;
constexpr int kBusiestFailures = 18, kOthersFailures = 8;

struct FeedReading
{
    Feed feed = Feed::Unknown;
    std::string why; // "stored into $7E:0D40", "TAX: becomes an index"
};

// Where the option's value goes. `body` is the function the option is in.
FeedReading feed_of(const ChangeOption &option, const FunctionBody &body, std::span<const uint8_t> rom,
                    std::span<const uint8_t> flags, const RomAddresses &where);

// The ROM bytes where a change is low risk, for rules aimed at low-risk changes only: in the
// given bodies, every option whose value feeds somewhere Low: a number's value bytes, a
// branch's or a math instruction's opcode, a call's whole instruction (a NOP-out skips it).
HotMask low_risk_mask(const FunctionBodies &bodies, std::span<const uint8_t> rom, std::span<const uint8_t> flags,
                      const RomAddresses &where);

} // namespace corrupt
