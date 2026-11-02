#include "instant_revert.h"

namespace corrupt
{

std::optional<InstantRevertResult> InstantRevert::after_frame(std::optional<DerailWatch::Note> note, uint64_t frame,
                                                              CoreAccess &core, RomBlame &blame)
{
    if (!note)
    {
        row = 0; // a clean frame
        return std::nullopt;
    }
    // The checkpoint must be the start of the frame that just ran: kept at the last frame's
    // count, or at this one when the count stood still (an STP stops the frame hook).
    if (!enabled || !state_frame || frame < *state_frame || frame > *state_frame + 1 || row >= kMaxInRow)
        return std::nullopt;
    std::vector<uint32_t> culprits;
    for (uint32_t o : note->culprits)
        if (blame.use(o))
            culprits.push_back(o);
    if (culprits.empty() || !core.load_state(state))
        return std::nullopt;
    auto fixed = blame.fix(culprits);
    row++;
    count++;

    InstantRevertResult out;
    out.kind = note->kind;
    out.pc = note->pc;
    out.frame = frame;
    out.offsets = std::move(fixed.offsets);
    out.rom_action = std::move(fixed.text);
    out.summary = "Caught at " + describe_pc(note->pc) + " (" + landing_text(note->kind) + "): " + out.rom_action +
                  " and played the frame again";
    out.headline = "Caught a bad jump: " + out.rom_action;
    return out;
}

JournalEntry journal_entry(const InstantRevertResult &r)
{
    JournalEntry e;
    e.kind = JournalEntry::InstantRevert;
    e.signal = landing_name(r.kind);
    e.text = std::string(landing_text(r.kind)) + " at " + describe_pc(r.pc);
    e.frame = r.frame;
    e.since_frame = r.frame;
    e.restored_frame = r.frame;
    e.frames_lost = 1;
    e.action = "none";
    e.attempt = 1;
    e.rom_blamed = r.offsets;
    e.rom_action = r.rom_action;
    return e;
}

void InstantRevert::keep(CoreAccess &core, uint64_t frame, bool protect)
{
    if (!enabled || !protect)
    {
        drop();
        return;
    }
    if (core.save_state_into(state))
        state_frame = frame;
    else
        drop();
}

void InstantRevert::drop()
{
    state_frame.reset();
}

} // namespace corrupt
