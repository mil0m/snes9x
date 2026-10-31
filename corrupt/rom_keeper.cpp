#include "rom_keeper.h"

#include <cstring>

namespace corrupt
{

void RomKeeper::game_loaded(const std::vector<AddressRange> &locked_ranges)
{
    size_t n = inner.size(Target::ROM);
    clean_rom.resize(n);
    if (auto raw = inner.data(Target::ROM))
        std::memcpy(clean_rom.data(), raw, n);
    else
        for (size_t i = 0; i < n; i++)
            clean_rom[i] = inner.read(Target::ROM, uint32_t(i));
    locked = {};
    for (auto &r : locked_ranges)
        if (r.target == Target::ROM)
            for (uint64_t a = r.first; a <= r.last && a < n; a += CrashMap::kBlock)
                locked.add_block(Target::ROM, uint32_t(a));
    // add_block works in 16-byte blocks; ranges that don't start on one are widened.
    journal.clear();
    changed_bytes = 0;
}

void RomKeeper::game_closed()
{
    clean_rom = {};
    locked = {};
    journal.clear();
    changed_bytes = 0;
}

void RomKeeper::put(uint32_t offset, uint8_t value)
{
    uint8_t before = inner.read(Target::ROM, offset);
    if (before == value)
        return;
    inner.write(Target::ROM, offset, value);
    uint8_t c = clean_rom[offset];
    if (before == c)
        changed_bytes++;
    else if (value == c)
        changed_bytes--;
}

void RomKeeper::write(Target t, uint32_t offset, uint8_t value)
{
    if (t != Target::ROM || !loaded())
        return inner.write(t, offset, value);
    if (offset >= clean_rom.size() || locked.is_hot(Target::ROM, offset))
        return;
    uint8_t before = inner.read(Target::ROM, offset);
    if (before == value)
        return;
    journal.push_back({ clock ? clock() : 0, offset, before });
    if (journal.size() > kJournalLimit)
        journal.erase(journal.begin(), journal.begin() + std::ptrdiff_t(kJournalLimit / 2));
    put(offset, value);
}

void RomKeeper::revert_after(uint64_t frame)
{
    while (!journal.empty() && journal.back().frame > frame)
    {
        put(journal.back().offset, journal.back().old_value);
        journal.pop_back();
    }
}

void RomKeeper::restore_clean()
{
    journal.clear();
    if (!loaded() || !changed_bytes)
        return;
    const uint8_t *raw = inner.data(Target::ROM);
    for (size_t i = 0; i < clean_rom.size() && changed_bytes; i++)
    {
        uint8_t now = raw ? raw[i] : inner.read(Target::ROM, uint32_t(i));
        if (now != clean_rom[i])
            put(uint32_t(i), clean_rom[i]);
    }
}

void RomKeeper::after_guard(const GuardOutcome &out)
{
    if (out.kind == GuardOutcome::RolledBack)
        revert_after(out.restored_frame);
    else if (out.kind == GuardOutcome::GaveUp)
        restore_clean();
}

} // namespace corrupt
