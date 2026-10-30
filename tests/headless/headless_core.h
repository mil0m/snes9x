#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The real Snes9x core with no window, sound or input: load a ROM, run frames,
// look at the picture and memory. One emulator per process (the core is global).
namespace headless
{

bool init();
bool load_rom_file(const std::string &path);
bool load_rom_memory(const std::vector<uint8_t> &rom);
void run_frames(int n);
uint64_t frame_hash(); // hash of the last rendered picture

std::vector<uint8_t> save_state();
bool load_state(const std::vector<uint8_t> &state);

// The core's messages (S9xMessage), newest last.
struct Message
{
    int type;
    std::string text;
};
const std::vector<Message> &messages();
void note_message(int type, const std::string &text);

} // namespace headless
