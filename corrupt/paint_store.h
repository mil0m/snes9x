#pragma once
#include "engine.h"
#include "paint_history.h"
#include "rom_keeper.h"

namespace corrupt
{

// Painting in a running game (#66): bytes through the ROM keeper as part of the run's start
// (a rollback doesn't undo paint; its own crash watch does), patches in the engine's recipe,
// and the locked and guarded bytes skipped.
class RecipePaintStore : public PaintStore
{
  public:
    RecipePaintStore(RomKeeper &rom, Engine &engine) : rom(rom), engine(engine) {}

    size_t size(Target t) const override { return rom.size(t); }
    uint8_t read(Target t, uint32_t offset) const override { return rom.read(t, offset); }
    void write(Target t, uint32_t offset, uint8_t value) override { rom.write_at_start(t, offset, value); }
    std::optional<uint8_t> patch(Target t, uint32_t offset) const override
    {
        return patch_byte(engine.recipe().patches, t, offset);
    }
    void set_patches(Target t, const std::vector<std::pair<uint32_t, std::optional<uint8_t>>> &list) override
    {
        std::vector<std::pair<uint32_t, uint8_t>> set;
        std::vector<uint32_t> gone;
        for (auto [o, v] : list)
            if (v)
                set.push_back({ o, *v });
            else
                gone.push_back(o);
        if (!gone.empty())
            engine.remove_patches(t, gone);
        if (!set.empty())
            engine.add_patches(t, set);
    }
    bool skip(Target t, uint32_t offset) const override
    {
        return rom.locked_mask().is_hot(t, offset) || engine.guarded_mask().is_hot(t, offset);
    }

  private:
    RomKeeper &rom;
    Engine &engine;
};

} // namespace corrupt
