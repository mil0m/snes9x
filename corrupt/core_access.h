#pragma once
#include "calls.h"
#include "code_map.h"
#include "crash_guard.h"
#include "derail.h"
#include "jump_tables.h"
#include "memory_map.h"
#include "memory_watch.h"

namespace corrupt
{

// CoreAccess over the live Snes9x core. Only call between frames.
class SnesCoreAccess : public CoreAccess
{
  public:
    std::vector<uint8_t> save_state() override;
    bool save_state_into(std::vector<uint8_t> &state) override;
    bool load_state(const std::vector<uint8_t> &state) override;
    FrameSignals sample() override;
    void reset_console() override; // S9xReset, then re-arms the reset watch (the ROM may have changed)
    // Test before applying (#52): frames run with nothing drawn (which also makes them
    // faster), the WRAM counters off, and the reset watch's count, the frameskip choice and
    // the on-screen message's timer put back at the end. The sound is the front end's to
    // drop while unseen() says so.
    void begin_unseen() override;
    void run_unseen_frame(bool draw = false) override;
    void end_unseen() override;
    static bool unseen() { return unseen_now; }
    // Kept moments (#47): the eight joypads' buttons, and every load and reset counted
    // (the front end's own loads and resets aren't: it tells the session).
    Pads pads() override;
    void set_pads(const Pads &p) override;
    uint64_t timeline() const override { return loads; }

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

    // Where each 4 KB of ROM shows up for the CPU: the bank games normally run it from
    // ($C0-$FF for HiROM, $80-$FF:8000 for LoROM), for disassembly and the viewer.
    static RomAddresses rom_addresses();
    // The reverse: for every 4 KB block of the CPU's address space, where it is in the ROM
    // (every mirror), to turn jump targets into ROM offsets.
    static RomOffsets rom_offsets();

    // The live layout for the memory viewer's map: where the PPU reads the picture
    // from VRAM, and where the sound DSP keeps its echo buffer and samples in ARAM.
    static VideoLayout video_layout();
    static SoundLayout sound_layout();

  private:
    bool drawing = true;
    uint64_t loads = 0;
    static inline bool unseen_now = false;
    struct
    {
        bool render = true;
        uint32_t info_timeout = 0;
        uint32_t reboots = 0;
        uint32_t *reads = nullptr, *writes = nullptr;
    } before_unseen;
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

// Records the code map while started: every instruction the CPU runs from ROM marks its
// opcode, operands and width, and the core marks the ROM bytes the CPU reads as data and
// DMA reads. Costs a wrapped call per instruction while on, nothing while off. Only one
// may record at a time. Start and stop between frames; the map must stay alive and keep
// its size until stop (or the tracer goes). With a jump log, the four indirect jumps
// (JMP/JSR (a,X), JMP (a), JML [a]) run from ROM are noted in it too (jump_tables.h), and
// with a call log the calls (JSR, JSL, JSR (a,X)) from ROM into ROM (calls.h, #26); they
// must stay alive until stop as well.
class RomTracer
{
  public:
    ~RomTracer() { stop(); }
    bool start(CodeMap &map, JumpLog *jumps = nullptr, CallLog *calls = nullptr); // false when another records, no ROM is loaded, or the size is wrong
    void stop();
    bool tracing() const { return mine; }

    // Instant revert (#51): while tracing, every instruction also goes to `watch` (null
    // stops), which notes the CPU landing on non-code right after a changed byte ran. Once
    // it has, the rest of the frame teaches the map nothing; only a changed byte that runs
    // still gets Touched, for the crash blame (#57). Take the note between frames, every
    // frame: that also turns marking back on.
    void watch_derails(DerailWatch *watch);
    // For measuring (corrupt-smoke --eyes): after a landing mark nothing at all, as before
    // #57. Reset by start().
    void set_blind_after_landing(bool on);
    // Changed bytes marked used after a landing that weren't already used that frame.
    uint64_t seen_after_landing() const;
    // Stops and resumes noting indirect jumps and calls, for frames that aren't real (a run
    // ahead, #52): a rejected change's jumps would teach it garbage tables.
    void note_jumps(bool on);
    std::optional<DerailWatch::Note> take_derail();

    // Every entry of the CPU's six opcode tables, for tests: what the tracer and the reset
    // watch change and must put back.
    static std::vector<uintptr_t> opcode_tables();

  private:
    bool mine = false;
    JumpLog *paused_jumps = nullptr;
    CallLog *paused_calls = nullptr;
};

} // namespace corrupt
