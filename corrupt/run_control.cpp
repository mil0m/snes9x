#include "run_control.h"

namespace corrupt
{

RunControl::Started RunControl::between_frames(Engine &engine, CoreAccess &core)
{
    if (!engine.waiting())
        return Started::Nothing;

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
        current = core.save_state();
    engine.release();
    return result;
}

} // namespace corrupt
