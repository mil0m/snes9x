#include "survivors.h"

#include <algorithm>

namespace corrupt
{

std::vector<Survivor> find_survivors(const RomBlame &blame, std::span<const uint8_t> clean, std::span<const uint8_t> now,
                                     std::span<const uint8_t> flags, const RomAddresses &where, uint32_t min_frames)
{
    std::vector<Survivor> out;
    for (uint32_t o : blame.changed_offsets())
    {
        const ByteUse *u = blame.use(o);
        if (!u || !u->survived || u->frames_used < min_frames || o >= clean.size() || o >= now.size())
            continue;
        Survivor s;
        s.offset = o;
        s.address = where.cpu_address(o);
        s.was = clean[o];
        s.now = now[o];
        s.frames_used = u->frames_used;
        s.first_used = u->first_used;
        s.flags = uint8_t((o < flags.size() ? flags[o] : 0) & codeflag::All);
        if (auto in = instruction_at(clean, flags, o, where))
            s.before = in->text;
        if (auto in = instruction_at(now, flags, o, where))
            s.after = in->text;
        out.push_back(std::move(s));
    }
    std::sort(out.begin(), out.end(), [](const Survivor &a, const Survivor &b) {
        return a.first_used != b.first_used ? a.first_used > b.first_used : a.offset < b.offset;
    });
    return out;
}

} // namespace corrupt
