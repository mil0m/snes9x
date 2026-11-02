#include "paint_history.h"

#include <algorithm>
#include <set>

namespace corrupt
{

namespace
{
size_t byte_count(const std::vector<PaintBytes> &strokes)
{
    std::set<uint32_t> seen;
    for (auto &s : strokes)
        for (auto &b : s)
            seen.insert(b.first);
    return seen.size();
}
} // namespace

void PaintHistory::drop_redo()
{
    entries.resize(active);
}

void PaintHistory::dab(Target t, const PaintBytes &bytes)
{
    if (open && open->target != t)
        close_open();
    if (!open)
    {
        drop_redo();
        open = PaintEntry{};
        open->kind = drafted(t) ? PaintEntry::Draft : PaintEntry::Live;
        open->target = t;
        open->strokes.emplace_back();
    }
    auto &stroke = open->strokes.front();
    std::vector<std::pair<uint32_t, std::optional<uint8_t>>> patches;
    for (auto [o, v] : bytes)
    {
        if (o >= store.size(t))
            continue;
        stroke.push_back({ o, v });
        if (open->kind != PaintEntry::Live || store.skip(t, o))
            continue;
        // Live: written now; the first time a byte is touched in this stroke keeps what was there.
        auto it = std::find_if(open->written.begin(), open->written.end(),
                               [o](const PaintEntry::Written &w) { return w.offset == o; });
        if (it == open->written.end())
            open->written.push_back({ o, store.read(t, o), v, store.patch(t, o) });
        else
            it->new_byte = v;
        store.write(t, o, v);
        patches.push_back({ o, v });
    }
    if (!patches.empty())
        store.set_patches(t, patches);
}

void PaintHistory::close_open()
{
    if (!open)
        return;
    if (!open->strokes.front().empty())
    {
        entries.push_back(std::move(*open));
        active = entries.size();
    }
    open.reset();
}

void PaintHistory::end_stroke()
{
    close_open();
}

void PaintHistory::undo_writes(const PaintEntry &e)
{
    std::vector<std::pair<uint32_t, std::optional<uint8_t>>> patches;
    for (auto it = e.written.rbegin(); it != e.written.rend(); ++it)
    {
        store.write(e.target, it->offset, it->old_byte);
        patches.push_back({ it->offset, it->old_patch });
    }
    if (!patches.empty())
        store.set_patches(e.target, patches);
}

void PaintHistory::redo_writes(const PaintEntry &e)
{
    std::vector<std::pair<uint32_t, std::optional<uint8_t>>> patches;
    for (auto &w : e.written)
    {
        store.write(e.target, w.offset, w.new_byte);
        patches.push_back({ w.offset, w.new_byte });
    }
    if (!patches.empty())
        store.set_patches(e.target, patches);
}

void PaintHistory::expand(size_t index)
{
    PaintEntry e = std::move(entries[index]);
    std::vector<PaintEntry> back;
    for (auto &s : e.strokes)
    {
        PaintEntry d;
        d.kind = PaintEntry::Draft;
        d.target = e.target;
        d.strokes = { s };
        back.push_back(std::move(d));
    }
    entries.erase(entries.begin() + long(index));
    entries.insert(entries.begin() + long(index), back.begin(), back.end());
}

bool PaintHistory::undo()
{
    close_open();
    if (active == 0)
        return false;
    auto &e = entries[active - 1];
    switch (e.kind)
    {
    case PaintEntry::Draft: active--; break;
    case PaintEntry::Live:
        undo_writes(e);
        active--;
        break;
    case PaintEntry::Applied:
    case PaintEntry::Discarded:
        if (e.kind == PaintEntry::Applied)
            undo_writes(e);
        drop_redo();
        expand(active - 1);
        active = entries.size();
        break;
    }
    return true;
}

bool PaintHistory::redo()
{
    close_open();
    if (active >= entries.size())
        return false;
    auto &e = entries[active];
    if (e.kind == PaintEntry::Live)
        redo_writes(e);
    active++;
    return true;
}

void PaintHistory::go_to(size_t count)
{
    close_open();
    while (active > count && undo())
        if (entries.size() < count) // a batch came apart: its strokes are now the newest
            break;
    while (active < count && redo())
    {
    }
}

std::map<uint32_t, uint8_t> PaintHistory::draft() const
{
    std::map<uint32_t, uint8_t> out;
    auto add = [&](const PaintEntry &e) {
        if (e.kind == PaintEntry::Draft)
            for (auto &s : e.strokes)
                for (auto [o, v] : s)
                    out[o] = v;
    };
    for (size_t i = 0; i < active; i++)
        add(entries[i]);
    if (open)
        add(*open);
    return out;
}

std::optional<PaintHistory::Applied> PaintHistory::apply()
{
    close_open();
    // The draft: the drafted strokes in effect, which a batch consumes.
    std::vector<size_t> drafted_at;
    for (size_t i = 0; i < active; i++)
        if (entries[i].kind == PaintEntry::Draft)
            drafted_at.push_back(i);
    if (drafted_at.empty())
        return std::nullopt;
    PaintEntry batch;
    batch.kind = PaintEntry::Applied;
    batch.target = Target::ROM;
    for (auto i : drafted_at)
        batch.strokes.push_back(entries[i].strokes.front());
    for (auto [o, v] : draft())
    {
        if (store.skip(Target::ROM, o))
        {
            batch.skipped++;
            continue;
        }
        batch.written.push_back({ o, store.read(Target::ROM, o), v, store.patch(Target::ROM, o) });
    }
    redo_writes(batch);
    drop_redo();
    for (auto it = drafted_at.rbegin(); it != drafted_at.rend(); ++it)
        entries.erase(entries.begin() + long(*it));
    entries.push_back(std::move(batch));
    active = entries.size();
    auto &e = entries.back();
    return Applied{ entries.size() - 1, e.written.size(), e.skipped };
}

void PaintHistory::discard_draft()
{
    close_open();
    std::vector<size_t> drafted_at;
    for (size_t i = 0; i < active; i++)
        if (entries[i].kind == PaintEntry::Draft)
            drafted_at.push_back(i);
    if (drafted_at.empty())
        return;
    PaintEntry gone;
    gone.kind = PaintEntry::Discarded;
    for (auto i : drafted_at)
        gone.strokes.push_back(entries[i].strokes.front());
    drop_redo();
    for (auto it = drafted_at.rbegin(); it != drafted_at.rend(); ++it)
        entries.erase(entries.begin() + long(*it));
    entries.push_back(std::move(gone));
    active = entries.size();
}

bool PaintHistory::take_back(size_t entry)
{
    close_open();
    if (entry >= active || entries[entry].kind != PaintEntry::Applied)
        return false;
    undo_writes(entries[entry]);
    drop_redo();
    // Live strokes after it lived in the state the restore point replaced.
    for (size_t i = entries.size(); i-- > entry + 1;)
        if (entries[i].kind == PaintEntry::Live)
            entries.erase(entries.begin() + long(i));
    expand(entry);
    active = entries.size();
    return true;
}

void PaintHistory::clear()
{
    entries.clear();
    active = 0;
    open.reset();
}

std::vector<PaintHistory::Row> PaintHistory::rows() const
{
    std::vector<Row> out;
    for (size_t i = entries.size(); i-- > 0;)
    {
        auto &e = entries[i];
        Row r;
        r.kind = e.kind;
        r.target = e.target;
        r.strokes = e.strokes.size();
        r.bytes = e.kind == PaintEntry::Applied ? e.written.size() : byte_count(e.strokes);
        r.skipped = e.skipped;
        r.active = i < active;
        r.index = i;
        uint32_t first = UINT32_MAX;
        std::set<uint8_t> values;
        for (auto &s : e.strokes)
            for (auto [o, v] : s)
            {
                first = std::min(first, o);
                values.insert(v);
            }
        r.first = first == UINT32_MAX ? 0 : first;
        r.value = values.size() == 1 ? *values.begin() : -1;
        out.push_back(r);
    }
    return out;
}

} // namespace corrupt
