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
        Danger,  // the crash map: crash rate
        Crashes, // the crash map: how many crashes
        Reads,   // how often the game reads each byte (WRAM)
        Writes,  // how often the game writes it (WRAM)
        Code,    // the code map: what the CPU runs (ROM), opcodes strong, operands lighter
        Data,    // what the CPU reads as data (ROM)
        Dma,     // what DMA copies (ROM)
        Changes, // bytes that differ from the clean ROM, stronger once the game used them
        Jumps,   // jump table entries (seen strong, likely lighter) and the instructions that jump through them
        Live,    // what the game runs and reads in the ROM right now, fading
        Functions, // the functions' code (#58): guarded stronger, an entry that's off strongest
        None,
    };

    // The colour an overlay paints in, for legends too.
    static QColor overlayColor(Overlay overlay)
    {
        return overlay == Overlay::Hits     ? QColor(150, 90, 230)
               : overlay == Overlay::Danger  ? QColor(225, 40, 40)
               : overlay == Overlay::Crashes ? QColor(240, 185, 20)
               : overlay == Overlay::Reads   ? QColor(40, 140, 240)
               : overlay == Overlay::Writes  ? QColor(40, 200, 80)
               : overlay == Overlay::Code    ? QColor(20, 200, 190)
               : overlay == Overlay::Data    ? QColor(235, 70, 160)
               : overlay == Overlay::Dma     ? QColor(190, 210, 30)
               : overlay == Overlay::Changes ? QColor(255, 120, 30)
               : overlay == Overlay::Jumps   ? QColor(95, 110, 255)
               : overlay == Overlay::Live    ? QColor(255, 225, 60)
               : overlay == Overlay::Functions ? QColor(60, 190, 120)
                                            : QColor(235, 110, 40);
    }

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
    // The busiest byte or block, for the overlays that count.
    struct OverlayScale
    {
        uint32_t hits = 1;
        uint32_t crashes = 1;
        float reads = 1.0f; // per frame
        float writes = 1.0f;
    };

    static OverlayScale overlayScale(const corrupt::MemorySnapshot &snap)
    {
        OverlayScale s;
        for (auto h : snap.hits)
            s.hits = std::max(s.hits, h);
        for (auto &b : snap.crash_blocks)
            s.crashes = std::max(s.crashes, b.crashes);
        for (auto r : snap.reads)
            s.reads = std::max(s.reads, r);
        for (auto w : snap.writes)
            s.writes = std::max(s.writes, w);
        return s;
    }

    // 0..1: how strongly to paint `offset` for the overlay.
    static float overlayLevel(const corrupt::MemorySnapshot &snap, Overlay overlay, size_t offset, const OverlayScale &scale)
    {
        constexpr float kActivityFull = 50.0f; // a byte the game changes every frame
        switch (overlay)
        {
        case Overlay::Activity:
            // Logarithmic, so a byte that changes once a second still shows.
            return offset < snap.heat.size() ? std::log1p(snap.heat[offset]) / std::log1p(kActivityFull) : 0.0f;
        case Overlay::Hits:
            return offset < snap.hits.size() && snap.hits[offset]
                       ? countLevel(snap.hits[offset], scale.hits)
                       : 0.0f;
        case Overlay::Danger:
            // Full strength from 0.6; a block that crashed once (1/3) shows at half.
            return offset < snap.danger.size() ? std::min(1.0f, snap.danger[offset] / 0.6f) : 0.0f;
        case Overlay::Crashes: {
            size_t block = offset / corrupt::CrashMap::kBlock;
            uint32_t c = block < snap.crash_blocks.size() ? snap.crash_blocks[block].crashes : 0;
            return c ? countLevel(c, scale.crashes) : 0.0f;
        }
        case Overlay::Reads:
            return offset < snap.reads.size() ? rateLevel(snap.reads[offset], scale.reads) : 0.0f;
        case Overlay::Writes:
            return offset < snap.writes.size() ? rateLevel(snap.writes[offset], scale.writes) : 0.0f;
        case Overlay::Code: {
            uint8_t f = offset < snap.code.size() ? snap.code[offset] : 0;
            return (f & corrupt::codeflag::Opcode) ? 1.0f : (f & corrupt::codeflag::Operand) ? 0.5f : 0.0f;
        }
        case Overlay::Data:
            return offset < snap.code.size() && (snap.code[offset] & corrupt::codeflag::Data) ? 1.0f : 0.0f;
        case Overlay::Dma:
            return offset < snap.code.size() && (snap.code[offset] & corrupt::codeflag::Dma) ? 1.0f : 0.0f;
        case Overlay::Changes: {
            auto *c = snap.rom_change(uint32_t(offset));
            return !c ? 0.0f : c->frames_used ? 1.0f : 0.45f;
        }
        case Overlay::Jumps: {
            uint8_t m = offset < snap.jumps.size() ? snap.jumps[offset] : 0;
            return m == corrupt::jumpmark::Seen ? 1.0f : m == corrupt::jumpmark::Site ? 0.7f : m ? 0.4f : 0.0f;
        }
        case Overlay::Live:
            return offset < snap.live.size() ? float(snap.live[offset]) / 255.0f : 0.0f;
        case Overlay::Functions: {
            uint8_t m = offset < snap.functions.size() ? snap.functions[offset] : 0;
            return m == corrupt::funcmark::Off       ? 1.0f
                   : m == corrupt::funcmark::Guarded ? 0.8f
                   : m == corrupt::funcmark::Body    ? 0.35f
                                                     : 0.0f;
        }
        case Overlay::None:
            return 0.0f;
        }
        return 0.0f;
    }

    // A count against the busiest: logarithmic, from a quarter so a single one still shows.
    static float countLevel(uint32_t n, uint32_t most)
    {
        return 0.25f + 0.75f * std::log1p(float(n)) / std::log1p(float(std::max(n, most)));
    }

    // Accesses per frame against the busiest, like countLevel. Below about once in 10 s
    // (or long faded), nothing.
    static float rateLevel(float per_frame, float most)
    {
        constexpr float kFloor = 0.001f;
        if (per_frame < kFloor)
            return 0.0f;
        return 0.25f + 0.75f * std::log1p(per_frame) / std::log1p(std::max(per_frame, most));
    }

    // Whether the overlay is about the crash map, so hot blocks get marked.
    static bool showsHot(Overlay overlay) { return overlay == Overlay::Danger || overlay == Overlay::Crashes; }
    static bool isHot(const corrupt::MemorySnapshot &snap, size_t offset)
    {
        size_t block = offset / corrupt::CrashMap::kBlock;
        return block < snap.hot_blocks.size() && snap.hot_blocks[block];
    }


    static QColor mix(const QColor &a, const QColor &b, float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                                a.blueF() + (b.blueF() - a.blueF()) * t);
    }
};
