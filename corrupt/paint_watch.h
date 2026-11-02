#pragma once
#include "crash_detector.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace corrupt
{

// The crash watch after Apply Paint (#66): for the next ten seconds of play, a crash means
// the paint did it. It keeps the restore point taken just before the batch went in, and
// which batch that was; the session loads the one and takes the other back. It watches
// whether or not a recipe is corrupting (the crash guard only watches while one is).
class PaintWatch
{
  public:
    static constexpr uint64_t kFrames = 600; // ten seconds

    explicit PaintWatch(DetectorConfig config = {}) : detector(config) {}

    // A batch went in: watch from the next frame played. A newer batch replaces an older
    // one still being watched (the older has stood up long enough to keep).
    void arm(std::vector<uint8_t> restore_point, size_t entry);
    void disarm();
    bool armed() const { return watching; }
    void set_config(const DetectorConfig &c) { detector.set_config(c); }

    enum class Result
    {
        Nothing, // not armed, or still watching
        Kept,    // the window passed without a crash
        Crashed, // load `state`, take back `entry`
    };
    struct Outcome
    {
        Result result = Result::Nothing;
        size_t entry = 0;
        std::optional<CrashVerdict> verdict;
        std::vector<uint8_t> state;
    };
    Outcome observe(const FrameSignals &frame); // once per frame played

  private:
    CrashDetector detector;
    bool watching = false;
    uint64_t frames = 0;
    size_t entry = 0;
    std::vector<uint8_t> state;
};

} // namespace corrupt
