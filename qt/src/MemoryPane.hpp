#pragma once
#include "corrupt/memory_watch.h"

#include <QColor>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>

// What the memory viewer does with a view of one target, whichever is showing: the hex
// grid (MemoryView) or the pixel view (PixelView).
class MemoryPane
{
  public:
    enum class Overlay
    {
        Activity,
        Hits,
        Danger, // the crash map
        None,
    };

    virtual ~MemoryPane() = default;

    // Keeps the selection and scroll position while the target and size stay the same.
    virtual void setSnapshot(const corrupt::MemorySnapshot &snapshot) = 0;
    virtual const corrupt::MemorySnapshot &snapshot() const = 0;
    virtual void setOverlay(Overlay overlay) = 0;

    virtual std::optional<std::pair<uint32_t, uint32_t>> selection() const = 0; // inclusive, first <= second
    virtual void select(uint32_t first, uint32_t last) = 0;                     // and scrolls it into view
    virtual void clearSelection() = 0;
    virtual void scrollTo(uint32_t offset) = 0; // its row becomes the first one
    virtual uint32_t firstVisibleOffset() const = 0;

    std::function<void()> onSelectionChanged;

  protected:
    // 0..1: how strongly to paint `offset` for the overlay.
    static float overlayLevel(const corrupt::MemorySnapshot &snap, Overlay overlay, size_t offset, uint32_t max_hits)
    {
        constexpr float kActivityFull = 50.0f; // a byte the game changes every frame
        switch (overlay)
        {
        case Overlay::Activity:
            // Logarithmic, so a byte that changes once a second still shows.
            return offset < snap.heat.size() ? std::log1p(snap.heat[offset]) / std::log1p(kActivityFull) : 0.0f;
        case Overlay::Hits:
            return offset < snap.hits.size() && snap.hits[offset]
                       ? 0.25f + 0.75f * std::log1p(float(snap.hits[offset])) / std::log1p(float(max_hits))
                       : 0.0f;
        case Overlay::Danger:
            // Full strength from 0.6; a block that crashed once (1/3) shows at half.
            return offset < snap.danger.size() ? std::min(1.0f, snap.danger[offset] / 0.6f) : 0.0f;
        case Overlay::None:
            return 0.0f;
        }
        return 0.0f;
    }

    static uint32_t maxHits(const corrupt::MemorySnapshot &snap)
    {
        uint32_t most = 1;
        for (auto h : snap.hits)
            most = std::max(most, h);
        return most;
    }

    static QColor overlayColor(Overlay overlay)
    {
        return overlay == Overlay::Hits     ? QColor(150, 90, 230)
               : overlay == Overlay::Danger ? QColor(225, 40, 40)
                                            : QColor(235, 110, 40);
    }

    static QColor mix(const QColor &a, const QColor &b, float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                                a.blueF() + (b.blueF() - a.blueF()) * t);
    }
};
