#pragma once
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
    LoopBranch,   // a branch back: a loop's exit or continue
    ForwardBranch,// a branch forward: skips or takes a block
    ResultUsed,   // a skipped call whose result (A, X, Y, flags) the caller reads straight after
    ResultUnused, // a skipped call whose caller sets what it needs again
    Unknown,      // the trail goes cold (a branch, a jump, or too far)
};

const char *feed_name(Feed f); // "variable", "index"...

struct FeedReading
{
    Feed feed = Feed::Unknown;
    std::string why; // "stored into $7E:0D40", "TAX: becomes an index"
};

// Where the option's value goes. `body` is the function the option is in.
FeedReading feed_of(const ChangeOption &option, const FunctionBody &body, std::span<const uint8_t> rom,
                    std::span<const uint8_t> flags, const RomAddresses &where);

} // namespace corrupt
