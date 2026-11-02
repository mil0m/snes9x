#pragma once
#include "memory.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace corrupt
{

// Where painting reads and writes (#66): the game's memories and the recipe's patches. The
// session implements it over the ROM keeper and the engine; tests use a map.
struct PaintStore
{
    virtual size_t size(Target t) const = 0;
    virtual uint8_t read(Target t, uint32_t offset) const = 0;
    virtual void write(Target t, uint32_t offset, uint8_t value) = 0; // into the game now
    virtual std::optional<uint8_t> patch(Target t, uint32_t offset) const = 0;
    // Many at once (the recipe's patch list is rebuilt on every change). Empty value: no patch.
    virtual void set_patches(Target t, const std::vector<std::pair<uint32_t, std::optional<uint8_t>>> &patches) = 0;
    virtual bool skip(Target, uint32_t) const { return false; } // locked or guarded (#58)
    virtual ~PaintStore() = default;
};

using PaintBytes = std::vector<std::pair<uint32_t, uint8_t>>; // offset, value

// One entry of the history.
struct PaintEntry
{
    enum Kind
    {
        Draft,     // a ROM stroke, not in the game yet
        Live,      // a stroke on another memory, written as it was drawn
        Applied,   // ROM strokes put in the game together by Apply Paint
        Discarded, // drafted strokes thrown away
    };
    struct Written
    {
        uint32_t offset;
        uint8_t old_byte, new_byte;
        std::optional<uint8_t> old_patch;
    };
    Kind kind = Draft;
    Target target = Target::ROM;
    std::vector<PaintBytes> strokes; // Draft and Live: one; Applied and Discarded: the strokes they took
    std::vector<Written> written;    // Live and Applied: what changed, to take it out again
    size_t skipped = 0;              // Applied: guarded bytes left alone
};

// The paint history (#66): every stroke can be undone and redone. ROM strokes are drafted
// and go into the game together with apply(), so a crash knows exactly what went in; the
// other memories are painted live. Undoing a batch (or a crash taking it back) returns its
// strokes to the draft. Consuming entries (Apply, Discard) can't be redone once undone:
// apply again instead.
class PaintHistory
{
  public:
    explicit PaintHistory(PaintStore &store) : store(store) {}

    static bool drafted(Target t) { return t == Target::ROM; }

    // A dab of the brush: part of the stroke being drawn until end_stroke().
    void dab(Target t, const PaintBytes &bytes);
    void end_stroke();
    void stroke(Target t, const PaintBytes &bytes) // a whole stroke at once (an imported picture)
    {
        dab(t, bytes);
        end_stroke();
    }

    bool undo();
    bool redo();
    bool can_undo() const { return active > 0 || open.has_value(); }
    bool can_redo() const { return active < entries.size(); }
    void go_to(size_t count); // keep the oldest `count` entries in effect

    std::map<uint32_t, uint8_t> draft() const; // ROM offset -> value, later strokes win
    bool has_draft() const { return !draft().empty(); }

    struct Applied
    {
        size_t entry;   // its index, for take_back()
        size_t bytes;   // written
        size_t skipped; // guarded
    };
    std::optional<Applied> apply(); // the draft into the game, as one entry
    void discard_draft();
    // A crash after Apply: the batch at `entry` comes out of the game and its strokes go back
    // to the draft; live strokes after it are dropped (the restore point undid them).
    bool take_back(size_t entry);
    void clear();

    struct Row
    {
        PaintEntry::Kind kind;
        Target target = Target::ROM;
        size_t strokes = 1;
        size_t bytes = 0;
        uint32_t first = 0;
        int value = -1; // the one value painted, or -1 for several
        size_t skipped = 0;
        bool active = true; // false: undone, can be redone
        size_t index = 0;   // go_to(index + 1) goes back to just after it
    };
    std::vector<Row> rows() const; // newest first

  private:
    void close_open();
    void drop_redo();
    void undo_writes(const PaintEntry &e);
    void redo_writes(const PaintEntry &e);
    void expand(size_t index); // a consuming entry back into its draft strokes

    PaintStore &store;
    std::vector<PaintEntry> entries;
    size_t active = 0; // entries[0, active) are in effect
    std::optional<PaintEntry> open;
};

} // namespace corrupt
