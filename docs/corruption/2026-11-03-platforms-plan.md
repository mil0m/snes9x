# Other emulators and platforms (#75)

Erik (3 Nov): "how much can we prepare during development so that it's easy to hook up other
emulators/platforms? If the editor code is completely separate from the emulation it should
(in theory) be easy to make implementations for other emulators. E.g. the editor is started
with a list of the memories there are (for SNES: WRAM 128 KB, ROM x MB, etc), sets up a
bunch of interop streams, and otherwise works the same." Mikael: "A large win."

## Where we are (measured 3 Nov, after #74)

We're closer than it sounds, by accident: the engine was written against interfaces so it
could be tested with fakes.

- **`corrupt/` (15,300 lines) touches the Snes9x core in two files only**: `core_access`
  and `core_memory` (about 150 lines touch `S9x…`, `Memory.`, `CPU.`, `PPU.`). Everything else
  talks to `MemoryAccess` (size/read/write per memory) and `CoreAccess` (save and load a
  state, sample a frame's signals, reset, run frames unseen). That is Erik's "list of
  memories and streams", in-process.
- **The engine already builds without the core**: `tests/CMakeLists.txt` compiles every
  `corrupt/*.cpp` except those two (and `hook`) into the tests.
- **The core changes are small**: a frame hook (`cpuexec.cpp`), WRAM read/write counters and
  ROM marks (`getset.h`, `dma.cpp`), a few flags in `ppu`/`apu`.
- **The UI already runs outside the emulator once**: the remote page (#53) drives the
  corruption over a WebSocket (status JSON, actions, `S9XM` memory messages), and the Qt
  panel and Workshop only see `CorruptionControl`.

What ties it to the SNES:

1. **The memories are a fixed enum**, `Target { WRAM, SRAM, VRAM, OAM, CGRAM, ARAM, ROM }`,
   with SNES sizes in `nominal_size`. About 1,020 lines name a `Target::`, but 800 of them
   are tests; the app and engine have about 220 (150 of them `Target::ROM`, mostly meaning
   "this is the game's code": code ops only on the ROM, the tracer's marks, locked header
   bytes, ROM keeper and blame). Recipes and crash maps store the memory by **name**
   (`"target": "wram"`), so files don't depend on the enum.
2. **SNES knowledge**: tile formats (`tile_decode`), palettes and byte colours, *What's
   where* (`memory_map`), the header and vectors that must not be corrupted, ROM export
   with checksum, LoROM/HiROM addresses, the PPU signals the crash detector reads (forced
   blank, brightness, NMI).
3. **65816 knowledge**: opcodes and their lengths, the tracer that wraps Snes9x's opcode
   tables, code aim, instruction help, calls/function bodies/kill switches (RTS/RTL), jump
   tables and pointer swap, derail watch and instant revert, function changes. About 2,500
   lines, plus the tracer in `core_access`.
4. **The Qt app's hub**, `CorruptionSession` (35 `Target::` uses, `SnesCoreAccess`, the
   frame hook), and the Snes9x front end around it.

## What else ties it to the SNES (from the review, checked)

Places the first draft called generic that aren't:

- **When in the frame we run.** The engine writes at line 1 of each frame, not between
  frames: at VBlank start most games copy sprites and palettes into OAM/CGRAM and would
  overwrite the corruption (`corrupt/hook.h`). A host that can only stop after a whole frame
  (libretro's `retro_run`) runs us at VBlank start. *When the host calls us* belongs in the
  host interface, and video-memory rules need a hook inside the emulator on any host.
- **The crash detector**: `FrameSignals` has a 24-bit PC, `$80:1234` formatting, the 65816's
  STP opcode, and PPU signals (NMI, forced blank, brightness) and reboots that come from the
  wrapped opcode tables. Only the picture hash, input, "CPU halted" and "emulation stopped"
  are generic.
- **60 fps**: seconds and frames are converted at 60 in the detector, guard, buttons, rule
  text, run ahead, and in recipe files (`recipe_json.cpp`: detection timings are stored in
  seconds).
- **24-bit limits and sizes**: every address in a recipe is checked against 0..0xFFFFFF;
  `crash_map.cpp` hard-codes Snes9x's SRAM buffer; hex digits per memory (`address_digits`).
- **Arrays sized by the enum**: `std::array<…, kTargetCount>` in the panel status, the memory
  viewer, the remote status, crash maps; the remote page's binary memory messages send the
  enum's index, so the page's JavaScript knows the list too.
- **Files with no memory or platform tag**: the code map file is a magic number and a size;
  the library's `codemap.`, `jumps.`, `calls.`, `names.` files are per game and assume one
  code memory. A game is its title plus the ROM's SHA-256.
- **The memory viewer's pictures**: tiles, pixels and the video/sound layouts are SNES
  formats (`SnesCoreAccess::video_layout`). Only the hex view is generic.
- **The CPU tier is patching, not an API**: the tracer wraps Snes9x's opcode tables and the
  WRAM counters live in `getset.h`. No other emulator offers that without equivalent
  patches.

## What would move, by tier

| Tier | Works on another emulator when… | Features |
|---|---|---|
| **Memory** | it can list its memories, read and write them at a good moment in the frame, save and load states, and give a frame's picture and input | rules on any memory (video memory only with a hook inside the frame), triggers, buttons, recipes, library and sharing, paint, the hex view, crash rollback with the picture signals (frozen, black), Back to Normal, run ahead, journal, remote page |
| **Platform** | someone writes that console's knowledge | tile, pixel and palette views, *What's where*, protected header bytes, ROM export, console-specific crash signals, frame rate |
| **CPU** | the emulator is patched so we see every instruction, and someone writes that CPU's tables | code map, code-aware ops, functions and kill switches, jump tables and pointer swap, instant revert, ROM blame by use, instruction help |

## Two boundaries, not one

Erik's "streams" fit the **UI ↔ engine** boundary (already true for the remote page): a
UI can live in another process. They don't fit the **engine ↔ emulator** boundary: the
engine writes inside the frame (line 1), and the crash guard, run ahead and instant revert
load states and run frames unseen between two frames, many times a second. A stream there
would be too slow and would race the emulator. So the engine stays in the emulator's
process (as a library), and the line to draw is the interface it is given.

## The plan (revised after the Fable review)

**Step 1: a `Platform` and a fake second platform, before any mass edit.** A `Platform`
describes the memories (id, name, size, digits, role), the frame rate and when in the frame
the host calls the engine. The SNES fills it with today's seven. A `FakePlatform` in
`tests/corrupt/` has two memories of other sizes, and the engine's tests run on both. This
finds the leaks above with tests, not by reading.

**Step 2: memories as roles plus a per-platform list.** Not a bare index: a small enum of
**roles** (code, work RAM, save, video, sprites, palette, sound) and the platform's list of
memories, each with a role. `Target::ROM` checks become role questions ("is this the game's
code?"), which keeps the code readable; the SNES exports constants (`snes::ROM`, …) so the
800 test lines keep compiling. In the same step, the leaks: frame rate from the platform,
address limits from the memory's size, arrays sized at run time, the detector split into
generic signals plus platform-named ones, the remote protocol carrying the memory list.
Production code is about 220 lines plus those; done in slices, tests green at each.

**Step 3: tag the files now.** The code map and crash map files get a platform/memory tag
while the formats are young, so no migration later.

**Step 4: the app talks to a host.** `CorruptionSession` gets an `EmulatorHost` (memories,
frame hook and its moment, states, unseen frames, signals, optional CPU trace) instead of
calling Snes9x; the Snes9x front end implements it. Pages hide what a host can't offer (no
Functions page without a CPU tier).

**Step 5: the proof, a headless libretro host first.** Like `tests/headless`: load a libretro
core, run `corrupt-smoke` on it, no audio, video or UI. Libretro offers what the memory tier
needs: memories (`retro_get_memory_data`, `retro_memory_map`), states (`retro_serialize`,
good enough for rewind and run-ahead in most cores), frames with audio and video off
(`GET_AUDIO_VIDEO_ENABLE`, our "unseen" frames), and the host owns the picture and input. The
first test core is Snes9x's own libretro port, which exposes only work RAM, save RAM and
video RAM (not the ROM, OAM, palette or sound RAM) unless we patch it. **Acceptance:** the
same recipe of plain random writes on those three memories, same seed, gives the same writes
through both hosts. Value-dependent ops and video-memory rules won't match (they run at a
different moment in the frame), and the plan says so. The Qt app loading libretro cores
comes after, and is a real front end (environment callbacks, pixel formats, audio, core
options): several sessions at least.

**Dropped from the first draft:**
- Splitting `corrupt/` into three CMake targets: the engine already builds without the core.
  An include check in `tools/` (no Snes9x header outside the two glue files) gives the same
  guarantee without moving files.
- A `CpuKnowledge` interface: with one CPU it would be shaped by the 65816. Wait until a
  host can trace a second one.
- A separate editor process over streams (see the two boundaries).

## Order and size

Steps 1–3 are worth doing even if no second platform ever comes: they make the SNES
assumptions explicit and tested, and they're where a port would spend most of its time.
Step 1 is a session; step 2 several, in slices; step 3 small; step 4 one or two; step 5 a
couple for the headless host, more for a full front end.

**Tests:** the engine's tests on the fake platform (step 1); every slice of step 2 green on
both platforms; old code map and crash map files still load (step 3); the include check in
`./build-mac.sh test` (no core header outside the glue); step 5's acceptance test above.

## Decision (3 Nov): parked, with two guards

Mikael asked first whether anything would be lost (no: Snes9x stays the full host, offering
the extras only it has; replays don't depend on how memories are numbered; SNES-only
features stay allowed), then how much doing it *now* gains, since hooking up any other
emulator means integration work in that emulator anyway. Not much: a port is three pieces
of work, generalizing the engine, an adapter inside the other emulator, and the interface
(about 21,000 lines of Qt that another emulator's own UI can't use), and doing the first
early doesn't make it smaller. With a real second host it also comes out right first time,
where guesses went wrong in the first draft. The exception is libretro *inside our own app*:
the interface comes along and one adapter covers many consoles; that's the moment to pick
this up. No separate repository until a second user of the engine exists (`corrupt/`
already builds alone; a subtree or submodule then).

Until then, two cheap guards so the coupling doesn't grow:
- `tools/engine_boundary_check.sh`, run by `./build-mac.sh test` (with its own test,
  `tests/tools/engine_boundary_check_test.sh`): nothing in `corrupt/` but the two glue files
  includes a Snes9x header or declares a core global.
- `is_code_memory(t)` in `memory.h`: new engine code asks it instead of `t == Target::ROM`;
  CLAUDE.md says so.
