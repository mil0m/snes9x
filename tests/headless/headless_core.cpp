#include "headless_core.h"

#include "snes9x.h"
#include "memmap.h"
#include "gfx.h"
#include "ppu.h"
#include "controls.h"
#include "snapshot.h"
#include "apu/apu.h"

namespace headless
{

namespace
{
std::vector<Message> message_log;
bool initialized = false;
} // namespace

const std::vector<Message> &messages()
{
    return message_log;
}

void note_message(int type, const std::string &text)
{
    message_log.push_back({ type, text });
}

bool init()
{
    if (initialized)
        return true;

    // Mirrors Snes9xController::init() in the Qt front end, so headless runs
    // behave like the real app. The cycle costs matter: with ALLOW_CPU_OVERCLOCK
    // they come from Settings, and zero would mean a frame never ends.
    memset(&Settings, 0, sizeof(Settings));
    Settings.Transparency = TRUE;
    Settings.Stereo = TRUE;
    Settings.SixteenBitSound = TRUE;
    Settings.HDMATimingHack = 100;
    Settings.InterpolationMethod = DSP_INTERPOLATION_GAUSSIAN;
    Settings.FrameTime = 16639;
    Settings.FrameTimeNTSC = 16639;
    Settings.FrameTimePAL = 20000;
    Settings.SoundPlaybackRate = 48000;
    Settings.SoundInputRate = 32040;
    Settings.BlockInvalidVRAMAccess = TRUE;
    Settings.SuperFXClockMultiplier = 100;
    Settings.MaxSpriteTilesPerLine = 34;
    Settings.OneClockCycle = 6;
    Settings.OneSlowClockCycle = 8;
    Settings.TwoClockCycles = 12;
    Settings.DontSaveOopsSnapshot = TRUE;
    CPU.Flags = 0;

    if (!Memory.Init() || !S9xInitAPU())
        return false;
    S9xInitSound(32);
    S9xSetSoundMute(TRUE);
    S9xGraphicsInit();
    S9xSetController(0, CTL_JOYPAD, 0, 0, 0, 0);
    S9xSetController(1, CTL_NONE, 0, 0, 0, 0);
    S9xUnmapAllControls();

    initialized = true;
    return true;
}

bool load_rom_file(const std::string &path)
{
    message_log.clear();
    return Memory.LoadROM(path.c_str());
}

bool load_rom_memory(const std::vector<uint8_t> &rom)
{
    message_log.clear();
    return Memory.LoadROMMem(rom.data(), uint32(rom.size()));
}

void run_frames(int n)
{
    for (int i = 0; i < n; i++)
        S9xMainLoop();
}

uint64_t frame_hash()
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int y = 0; y < IPPU.RenderedScreenHeight; y++)
    {
        const uint16 *row = GFX.Screen + y * GFX.RealPPL;
        for (int x = 0; x < IPPU.RenderedScreenWidth; x++)
        {
            h ^= row[x];
            h *= 0x100000001b3ULL;
        }
    }
    return h;
}

std::vector<uint8_t> save_state()
{
    std::vector<uint8_t> state(S9xFreezeSize());
    if (!S9xFreezeGameMem(state.data(), uint32(state.size())))
        state.clear();
    return state;
}

bool load_state(const std::vector<uint8_t> &state)
{
    if (S9xUnfreezeGameMem(state.data(), uint32(state.size())) != SUCCESS)
        return false;
    // The CPU flags come back from the state; the core's own stop switch (set by a deadlock)
    // doesn't. Left set, every run after a deadlocked one looked dead.
    Settings.StopEmulation = FALSE;
    return true;
}

} // namespace headless
