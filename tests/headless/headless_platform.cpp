// The platform functions the Snes9x core expects from a front end, for a
// front end with no window, no sound, no input and no files.
#include "snes9x.h"
#include "memmap.h"
#include "gfx.h"
#include "display.h"
#include "controls.h"
#include "fscompat.h"
#include "apu/apu.h"

#include "headless_core.h"

#include <filesystem>

bool8 S9xInitUpdate(void) { return TRUE; }
bool8 S9xDeinitUpdate(int, int) { return TRUE; }
bool8 S9xContinueUpdate(int, int) { return TRUE; }

void S9xSyncSpeed(void)
{
    // Render every frame (no frameskip) so frame hashes see everything.
    IPPU.RenderThisFrame = TRUE;
    IPPU.FrameSkip = 0;
    IPPU.SkippedFrames = 0;
}

void S9xMessage(int type, int, const char *message)
{
    headless::note_message(type, message ? message : "");
}

void S9xExit(void) {}
void S9xAutoSaveSRAM(void) {}
bool8 S9xOpenSoundDevice(void) { return TRUE; }
void S9xToggleSoundChannel(int) {}

bool8 S9xOpenSnapshotFile(const char *, bool8, STREAM *) { return FALSE; }
void S9xCloseSnapshotFile(STREAM) {}
const char *S9xStringInput(const char *) { return nullptr; }

std::string S9xGetDirectory(enum s9x_getdirtype)
{
    return std::filesystem::temp_directory_path().string();
}

std::string S9xGetFilenameInc(std::string, enum s9x_getdirtype)
{
    return {};
}

bool S9xPollButton(uint32, bool *pressed)
{
    *pressed = false;
    return false;
}
bool S9xPollPointer(uint32, int16 *, int16 *) { return false; }
bool S9xPollAxis(uint32, int16 *) { return false; }
void S9xHandlePortCommand(s9xcommand_t, int16, int16) {}
