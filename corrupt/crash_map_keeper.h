#pragma once
#include "crash_guard.h"
#include "crash_learner.h"
#include "library.h"

namespace corrupt
{

// The loaded game's crash map while playing: this person's counts (the only ones saved),
// everyone else's from the library, the learner that adds to them, and the hot mask the
// engine steers by. Lives on the emulation thread, called between frames.
class CrashMapKeeper
{
  public:
    static constexpr uint64_t kSaveEveryFrames = 3600; // a minute, while counts change

    void reset(CrashMaps loaded = {}); // a game was loaded (or closed)

    // Every frame, after it ran.
    void observe(const std::deque<LoggedEvent> &log, uint64_t frame);
    // After the guard rolled back. True when the hot set changed: give the engine hot_mask().
    bool crashed(const GuardOutcome &out);

    bool save_due() const;
    bool unsaved() const { return dirty; } // anything learned since the last save, e.g. on close
    bool saved(); // call after writing mine(); true when the hot set changed meanwhile

    const CrashMap &mine() const { return own; }
    const CrashMap &combined() const { return all; }
    const HotMask &hot_mask() const { return mask; }
    size_t hot_blocks() const { return all.hot_blocks(); }
    std::vector<float> danger(Target t, size_t size) const; // per byte, 0..1, for the viewer

  private:
    bool rebuild(); // true when the hot set changed

    CrashMap own, others, all;
    CrashLearner learner;
    HotMask mask;
    bool dirty = false;
    bool crash_unsaved = false;
    uint64_t frames_since_save = 0;
};

} // namespace corrupt
