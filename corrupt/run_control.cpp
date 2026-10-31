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
        classic = engine.fire_at_load(*rom, [](Target t) { return t == Target::ROM; }) > 0 ||
                  engine.has_at_load(Target::ROM);
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
        engine.fire_at_load(*rom, [](Target t) { return t != Target::ROM; });
    engine.release();
    return result;
}

} // namespace corrupt
