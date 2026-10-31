#pragma once
#include "crash_guard.h"
#include "memory_map.h"
#include "memory_watch.h"

namespace corrupt
{

// CoreAccess over the live Snes9x core. Only call between frames.
class SnesCoreAccess : public CoreAccess
{
  public:
    std::vector<uint8_t> save_state() override;
    bool load_state(const std::vector<uint8_t> &state) override;
    FrameSignals sample() override;
    void reset_console() override; // S9xReset, then re-arms the reset watch (the ROM may have changed)

    // The ROM bytes the CPU sees at $00:FFB0-$FFFF (the header and the interrupt vectors),
    // wherever this game's mapping puts them. Nothing may corrupt them.
    static std::vector<AddressRange> locked_rom();

    // The ROM as it is now, as a clean .sfc (no copier header), for Export ROM. With
    // fix_checksum, the header's checksum and complement match the corrupted contents,
    // so other emulators and flash carts don't complain.
    static std::vector<uint8_t> rom_image(bool fix_checksum = true);

    // Call from the frame hook: remembers whether this frame is being drawn
    // (with frameskip it isn't, and the screen buffer keeps the last picture).
    void note_frame_start();

    // Counts the game jumping back into its own reset code (FrameSignals::reboots).
    // Wraps the reset handler's first opcode in the CPU's opcode tables, so call it
    // after every ROM load (it re-targets itself) and unwatch when the game closes.
    static void watch_resets();
    static void unwatch_resets();

    // The live layout for the memory viewer's map: where the PPU reads the picture
    // from VRAM, and where the sound DSP keeps its echo buffer and samples in ARAM.
    static VideoLayout video_layout();
    static SoundLayout sound_layout();

  private:
    bool drawing = true;
};

// The game's WRAM reads and writes, counted by the core (getset.h) while started, for the
// memory viewer. Only one may count at a time; start, stop and take between frames.
class WramAccessCounter
{
  public:
    ~WramAccessCounter() { stop(); }
    void start();
    void stop(); // the core stops counting; the counts are dropped
    bool counting() const { return !reads.empty(); }
    // Hands the counts since the last take to the watch, which starts them over.
    void take(MemoryWatch &watch);

  private:
    std::vector<uint32_t> reads, writes;
};

} // namespace corrupt
