#include "paint_watch.h"

namespace corrupt
{

void PaintWatch::arm(std::vector<uint8_t> restore_point, size_t batch)
{
    state = std::move(restore_point);
    entry = batch;
    frames = 0;
    watching = true;
    detector.reset();
}

void PaintWatch::disarm()
{
    watching = false;
    state.clear();
}

PaintWatch::Outcome PaintWatch::observe(const FrameSignals &frame)
{
    Outcome out;
    if (!watching)
        return out;
    out.entry = entry;
    if (auto verdict = detector.observe(frames, frame))
    {
        out.result = Result::Crashed;
        out.verdict = verdict;
        out.state = std::move(state);
        disarm();
        return out;
    }
    if (++frames >= kFrames)
    {
        out.result = Result::Kept;
        disarm();
    }
    return out;
}

} // namespace corrupt
