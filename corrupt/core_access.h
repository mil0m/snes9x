#pragma once
#include "crash_guard.h"

namespace corrupt
{

// CoreAccess over the live Snes9x core. Only call between frames.
class SnesCoreAccess : public CoreAccess
{
  public:
    std::vector<uint8_t> save_state() override;
    bool load_state(const std::vector<uint8_t> &state) override;
    FrameSignals sample() override;

    // Call from the frame hook: remembers whether this frame is being drawn
    // (with frameskip it isn't, and the screen buffer keeps the last picture).
    void note_frame_start();

    // Counts the game jumping back into its own reset code (FrameSignals::reboots).
    // Wraps the reset handler's first opcode in the CPU's opcode tables, so call it
    // after every ROM load (it re-targets itself) and unwatch when the game closes.
    static void watch_resets();
    static void unwatch_resets();

  private:
    bool drawing = true;
};

} // namespace corrupt
