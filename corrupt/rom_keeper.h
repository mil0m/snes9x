#pragma once
#include "crash_guard.h"
#include "crash_map.h"
#include "memory.h"

#include <deque>
#include <functional>
#include <vector>

namespace corrupt
{

// The ROM during a run: the clean ROM plus that run's edits.
//
// A MemoryAccess over the emulator's. It keeps a copy of the clean ROM taken when the
// game loaded, refuses writes to the locked bytes (the header and interrupt vectors),
// and journals every ROM edit with the frame it happened in. Save states don't hold the
// ROM, so this is what lets a rollback undo ROM edits too. Other targets pass straight
// through. Lives on the emulation thread.
class RomKeeper : public MemoryAccess
{
  public:
    static constexpr size_t kJournalLimit = size_t(1) << 20; // ~16 MB; rollbacks only reach back ~30 s

    explicit RomKeeper(MemoryAccess &inner) : inner(inner) {}

    // The frame ROM edits are stamped with: the engine's current frame. Unset: 0.
    void set_clock(std::function<uint64_t()> clock) { this->clock = std::move(clock); }

    // A game was loaded: keeps a copy of its (still clean) ROM and the bytes nobody may change.
    void game_loaded(const std::vector<AddressRange> &locked);
    void game_closed();
    bool loaded() const { return !clean_rom.empty(); }

    size_t size(Target t) const override { return inner.size(t); }
    uint8_t read(Target t, uint32_t offset) const override { return inner.read(t, offset); }
    const uint8_t *data(Target t) const override { return inner.data(t); }
    void write(Target t, uint32_t offset, uint8_t value) override;
    // A write that belongs to the run's start (a painted patch): never undone by a rollback,
    // only by restore_clean. Not journalled, so the journal stays in time order. (A live
    // edit of the same byte from before the paint would still revert to its old value.)
    void write_at_start(Target t, uint32_t offset, uint8_t value) override;

    // For the engine, so rules skip the locked bytes instead of logging writes that never land.
    const HotMask &locked_mask() const { return locked; }

    // Called whenever a byte starts (true) or stops (false) differing from the clean ROM.
    std::function<void(uint32_t offset, bool changed)> on_change;
    // Called when a game loads or closes: every byte is clean again, without a notice each.
    std::function<void()> on_forget;

    // Writes that stand for the rest of the run, like a crash policy's fix: no rollback
    // undoes them, because the bytes' journal entries go too. One pass over the journal.
    void settle(const std::vector<std::pair<uint32_t, uint8_t>> &bytes);

    // A value in the ROM for a moment (a run ahead trying a byte without its change): no
    // journal entry, no notice, no count. The caller puts the old value back the same way
    // before anything else reads or writes the ROM.
    void borrow(uint32_t offset, uint8_t value)
    {
        if (loaded() && offset < clean_rom.size())
            inner.write(Target::ROM, offset, value);
    }

    void restore_clean();               // the run starts over: every edit undone, the journal emptied
    void revert_after(uint64_t frame);  // undo the edits made after `frame`, newest first
    void after_guard(const GuardOutcome &out); // a rollback reverts; giving up restores the clean ROM

    size_t changed() const { return changed_bytes; } // bytes that differ from the clean ROM
    const std::vector<uint8_t> &clean() const { return clean_rom; }
    size_t journal_size() const { return journal.size(); }

  private:
    struct Edit
    {
        uint64_t frame;
        uint32_t offset;
        uint8_t old_value;
    };

    void put(uint32_t offset, uint8_t value); // writes and keeps changed_bytes right

    MemoryAccess &inner;
    std::function<uint64_t()> clock;
    std::vector<uint8_t> clean_rom;
    HotMask locked;
    std::deque<Edit> journal;
    size_t changed_bytes = 0;
};

} // namespace corrupt
