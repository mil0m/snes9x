#include "derail.h"

#include <algorithm>

namespace corrupt
{

const char *landing_text(Landing l)
{
    switch (l)
    {
    case Landing::None: return "kept to its code";
    case Landing::OpenBus: return "jumped into empty space";
    case Landing::MidInstruction: return "jumped into the middle of an instruction";
    case Landing::Brk: return "ran into empty ROM (BRK)";
    case Landing::Stop: return "stopped the CPU (STP)";
    case Landing::Wdm: return "ran an unused opcode (WDM)";
    }
    return "?";
}

const char *landing_name(Landing l)
{
    switch (l)
    {
    case Landing::None: return "none";
    case Landing::OpenBus: return "open_bus";
    case Landing::MidInstruction: return "mid_instruction";
    case Landing::Brk: return "brk";
    case Landing::Stop: return "stp";
    case Landing::Wdm: return "wdm";
    }
    return "?";
}

void DerailWatch::land(Landing kind, uint32_t pc)
{
    if (!open() || kind == Landing::None)
        return;
    Note n;
    n.kind = kind;
    n.pc = pc & 0xFFFFFF;
    size_t kept = std::min<size_t>(next, kRing);
    for (size_t i = 0; i < kept; i++)
    {
        const Use &u = ring[(next - 1 - i) % kRing];
        if (count - u.at > kWindow)
            break; // older ones are older still
        if (std::find(n.culprits.begin(), n.culprits.end(), u.offset) == n.culprits.end())
            n.culprits.push_back(u.offset);
    }
    note = std::move(n);
    open_end = 0; // quiet until the note is taken
}

std::optional<DerailWatch::Note> DerailWatch::take()
{
    auto out = std::move(note);
    note.reset();
    if (out)
    {
        next = 0;
        open_end = held_open ? UINT64_MAX : 0;
    }
    return out;
}

void DerailWatch::clear()
{
    note.reset();
    next = 0;
    open_end = held_open ? UINT64_MAX : 0;
}

} // namespace corrupt
