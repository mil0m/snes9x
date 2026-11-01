#include "run_control.h"

namespace corrupt
{

RunControl::Started RunControl::between_frames(Engine &engine, CoreAccess &core, RomKeeper *rom)
{
    if (!engine.waiting())
        return Started::Nothing;

    bool classic = false;
    if (rom)
    {
        rom->restore_clean();
        pins = engine.apply_pins(*rom); // before the rules, onto the clean ROM
        engine.apply_switches(*rom);     // kill switches that start off (#34)
        auto is_rom = [](Target t) { return t == Target::ROM; };
        engine.fire_at_load(*rom, is_rom);
        engine.apply_patches(*rom, is_rom);
        classic = engine.has_at_load(Target::ROM) || engine.has_patches(Target::ROM) || pins.written > 0;
    }

    Started result = Started::Fresh;
    from_saved = false;
    if (pending)
    {
        if (core.load_state(*pending))
        {
            current = std::move(*pending);
            from_saved = true;
            result = Started::FromAnchor;
        }
        else
            result = Started::AnchorFailed;
        pending.reset();
    }
    if (!from_saved)
    {
        if (classic)
            core.reset_console();
        current = core.save_state();
    }
    if (rom)
    {
        auto not_rom = [](Target t) { return t != Target::ROM; };
        engine.fire_at_load(*rom, not_rom);
        engine.apply_patches(*rom, not_rom);
    }
    engine.release();
    return result;
}

} // namespace corrupt
