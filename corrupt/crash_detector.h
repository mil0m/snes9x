#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace corrupt
{

enum class PcClass
{
    Ok,
    Io,      // executing hardware registers
    OpenBus, // executing unmapped memory
};

// What the emulator looks like after one frame. Sampled between frames.
struct FrameSignals
{
    bool cpu_halted = false;        // STP, or the core gave up on the CPU
    bool emulation_stopped = false; // the core's own fatal stop ("CPU is deadlocked")
    PcClass pc_class = PcClass::Ok;
    uint32_t pc = 0;                 // 24-bit bank:address
    std::optional<uint8_t> opcode;   // the byte at pc, when it is plain ROM/RAM
    bool nmi_enabled = true;
    bool forced_blank = false;
    uint8_t brightness = 15;
    uint64_t picture_hash = 0;
    bool picture_valid = true; // false when the frame wasn't drawn (frameskip): the hash is stale
    uint16_t input = 0;        // controller 1 buttons
    uint32_t reboots = 0;      // running count of times the game re-entered its reset code
};

enum class CrashSignal
{
    CpuStopped,
    Deadlock,
    RunawayPc,
    NmiOff,
    FrozenPicture,
    BlackScreen,
    Manual, // the player said so
    Reboot, // the game jumped back into its own reset code
};

const char *crash_signal_name(CrashSignal s);

// Thresholds in frames; 0 switches a signal off.
struct DetectorConfig
{
    bool enabled = true;
    bool cpu_stopped = true;
    // Sampled once per frame, so it rarely fires: a CPU running from open bus usually
    // branches back into ROM within the frame (verified with generated test ROMs).
    uint32_t runaway_frames = 30;
    // Clean 5-minute runs of four games showed natural NMI-off stretches up to 2.7 s.
    uint32_t nmi_off_frames = 300;
    uint32_t frozen_frames = 300;
    uint32_t black_frames = 480;
    // Frozen and black screens only count while the player presses buttons and nothing
    // reacts; a still screen nobody is touching (a text box, a menu) is not a crash.
    uint32_t min_presses = 3;
    // A game rebooting itself looks normal from outside (it shows its title screen);
    // the core glue counts entries into the reset code made in native mode.
    bool reboot = true;
};

struct CrashVerdict
{
    CrashSignal signal;
    uint64_t frame;       // when it was noticed
    uint64_t since_frame; // when the trouble started; rollback goes back before this
    bool heuristic;       // a guess (NMI off, frozen, black): only trusted if corruption happened
    std::string text;
};

class CrashDetector
{
  public:
    explicit CrashDetector(DetectorConfig config = {}) : config(config) {}

    // Call once per frame with increasing frame numbers.
    std::optional<CrashVerdict> observe(uint64_t frame, const FrameSignals &s);
    void reset();
    void set_config(const DetectorConfig &c) { config = c; reset(); }
    const DetectorConfig &get_config() const { return config; }

  private:
    DetectorConfig config;
    std::optional<uint64_t> runaway_since;
    std::optional<uint64_t> nmi_off_since;
    std::optional<uint64_t> black_since;
    std::optional<uint64_t> frozen_since;
    std::optional<uint64_t> last_hash;
    std::optional<uint16_t> last_input;
    std::optional<uint32_t> last_reboots;
    uint32_t black_presses = 0;
    uint32_t frozen_presses = 0;
};

std::string describe_pc(uint32_t pc); // "$80:1234"

} // namespace corrupt
