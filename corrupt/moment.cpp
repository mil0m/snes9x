#include "moment.h"

#include "nlohmann/json.hpp"

#ifdef ZLIB
#include <zlib.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace corrupt
{

namespace
{

// What a saved state holds: everything but the game's code (save states don't hold the ROM).
bool in_state(Target t) { return !is_code_memory(t); }

uint8_t byte_at(const MemoryAccess &mem, const uint8_t *data, Target t, uint32_t i)
{
    return data ? data[i] : mem.read(t, i);
}

} // namespace

void MomentRecorder::reset()
{
    keys.clear();
    frames.clear();
    pending.clear();
    first = now = 0;
    seen.reset();
    cut_due = true;
    cut_due_why.clear();
    cut_why.clear();
    cut_at = 0;
    in_hook = held = false;
}

void MomentRecorder::cut(const std::string &why)
{
    if (held)
        return;
    cut_due = true;
    cut_due_why = why;
}

void MomentRecorder::before_frame(CoreAccess &core, const MemoryAccess &mem, std::span<const uint8_t> clean,
                                  uint64_t timeline)
{
    if (held)
        return;
    if (seen && *seen != timeline && !cut_due)
    {
        cut_due = true;
        cut_due_why = "the game jumped to another moment";
    }
    seen = timeline;
    if (cut_due)
    {
        keys.clear();
        frames.clear();
        pending.clear();
        first = now;
    }
    if (keys.empty() || now - keys.back().at >= cfg.keyframe_every)
    {
        Keyframe k;
        k.at = now;
        if (!core.save_state_into(k.state))
        {
            keys.clear();
            frames.clear();
            pending.clear();
            cut_due = true;
            return;
        }
        size_t n = std::min(mem.size(Target::ROM), clean.size());
        const uint8_t *d = mem.data(Target::ROM);
        for (size_t i = 0; i < n; i++)
            if (uint8_t v = byte_at(mem, d, Target::ROM, uint32_t(i)); v != clean[i])
                k.rom.emplace_back(uint32_t(i), v);
        pending.clear(); // in the keyframe now
        keys.push_back(std::move(k));
        if (cut_due)
        {
            cut_why = cut_due_why;
            cut_at = now;
        }
        while (keys.size() > std::max<size_t>(cfg.keyframes, 1))
        {
            keys.pop_front();
            while (first < keys.front().at && !frames.empty())
            {
                frames.pop_front();
                first++;
            }
        }
    }
    cut_due = false;
    MomentFrame f;
    f.pads = core.pads();
    f.before = std::move(pending);
    pending.clear();
    frames.push_back(std::move(f));
    now++;
}

void MomentRecorder::wrote(Target t, uint32_t offset, uint8_t value)
{
    if (held || cut_due)
        return;
    if (in_hook)
    {
        if (!frames.empty())
            frames.back().during.push_back({ t, offset, value });
    }
    else
        pending.push_back({ t, offset, value });
}

void MomentRecorder::redo_frame(uint64_t timeline)
{
    if (held)
        return;
    seen = timeline;
    if (cut_due || frames.empty())
        return;
    MomentFrame f = std::move(frames.back());
    frames.pop_back();
    now--;
    // The load put back everything but the ROM: what the frame and the time since wrote there
    // stands, and goes before the frame played again.
    std::vector<MomentWrite> before = std::move(f.before);
    for (auto &w : f.during)
        if (!in_state(w.target))
            before.push_back(w);
    for (auto &w : pending)
        if (!in_state(w.target))
            before.push_back(w);
    pending = std::move(before);
}

uint64_t MomentRecorder::frames_to_keep() const
{
    if (cut_due || keys.empty())
        return 0;
    for (auto it = keys.rbegin(); it != keys.rend(); ++it)
        if (now - it->at >= cfg.lead)
            return now - it->at;
    return now - keys.front().at;
}

std::optional<Moment> MomentRecorder::take() const
{
    if (cut_due || keys.empty())
        return std::nullopt;
    const Keyframe *k = &keys.front();
    for (auto it = keys.rbegin(); it != keys.rend(); ++it)
        if (now - it->at >= cfg.lead)
        {
            k = &*it;
            break;
        }
    Moment m;
    m.state = k->state;
    m.rom = k->rom;
    m.frames.assign(frames.begin() + std::ptrdiff_t(k->at - first), frames.end());
    m.after = pending;
    m.from = k->at;
    if (k->at == cut_at)
        m.starts = cut_why;
    return m;
}

bool MomentPlayer::start(Moment m, CoreAccess &core, MemoryAccess &mem, std::span<const uint8_t> clean)
{
    on = false;
    if (!core.load_state(m.state))
        return false;
    // The ROM as it was then: the moment's bytes, and the clean ROM everywhere else.
    size_t n = std::min(mem.size(Target::ROM), clean.size());
    const uint8_t *d = mem.data(Target::ROM);
    size_t j = 0;
    for (size_t i = 0; i < n; i++)
    {
        uint8_t want = clean[i];
        while (j < m.rom.size() && m.rom[j].first < i)
            j++;
        if (j < m.rom.size() && m.rom[j].first == i)
            want = m.rom[j].second;
        if (byte_at(mem, d, Target::ROM, uint32_t(i)) != want)
            mem.write(Target::ROM, uint32_t(i), want);
    }
    moment = std::move(m);
    next = 0;
    hook_frame.reset();
    on = true;
    return true;
}

bool MomentPlayer::before_frame(CoreAccess &core, MemoryAccess &mem)
{
    hook_frame.reset();
    if (!on)
        return false;
    if (next >= moment.frames.size())
    {
        for (auto &w : moment.after)
            mem.write(w.target, w.offset, w.value);
        on = false;
        return false;
    }
    auto &f = moment.frames[next];
    set = f.pads;
    core.set_pads(f.pads);
    for (auto &w : f.before)
        mem.write(w.target, w.offset, w.value);
    hook_frame = next++;
    return true;
}

void MomentPlayer::in_hook(MemoryAccess &mem)
{
    if (!hook_frame)
        return;
    for (auto &w : moment.frames[*hook_frame].during)
        mem.write(w.target, w.offset, w.value);
    hook_frame.reset();
}

bool MomentPlayer::taken_over(CoreAccess &core) const
{
    return on && next > 0 && core.pads() != set;
}

MomentCheck check_moment(const Moment &m, MomentPlayer &player, CoreAccess &core, MemoryAccess &mem,
                         std::span<const uint8_t> clean, const CheckHooks &hooks)
{
    auto t0 = std::chrono::steady_clock::now();
    MomentCheck c;
    Pads pads = core.pads();
    std::vector<uint8_t> state = core.save_state();
    if (state.empty())
    {
        c.text = "It couldn't be checked: the game's state couldn't be saved.";
        return c;
    }
    std::array<std::vector<uint8_t>, kTargetCount> was;
    for (auto t : kAllTargets)
    {
        auto &v = was[size_t(t)];
        v.resize(mem.size(t));
        const uint8_t *d = mem.data(t);
        for (size_t i = 0; i < v.size(); i++)
            v[i] = byte_at(mem, d, t, uint32_t(i));
    }
    std::vector<uint8_t> flags(hooks.flags.begin(), hooks.flags.end());
    if (hooks.begin)
        hooks.begin();
    core.begin_unseen();
    if (player.start(m, core, mem, clean))
    {
        c.played = true;
        while (player.before_frame(core, mem))
        {
            core.run_unseen_frame(hooks.draw);
            c.frames++;
        }
    }
    core.end_unseen();
    player.stop();
    if (c.played)
        for (auto t : kAllTargets)
        {
            auto &v = was[size_t(t)];
            const uint8_t *d = mem.data(t);
            bool same = mem.size(t) == v.size();
            for (size_t i = 0; same && i < v.size(); i++)
                same = byte_at(mem, d, t, uint32_t(i)) == v[i];
            if (!same)
                c.differ.push_back(t);
        }
    // Everything back as it was.
    core.load_state(state);
    {
        auto &rom = was[size_t(Target::ROM)];
        const uint8_t *d = mem.data(Target::ROM);
        for (size_t i = 0; i < rom.size(); i++)
            if (byte_at(mem, d, Target::ROM, uint32_t(i)) != rom[i])
                mem.write(Target::ROM, uint32_t(i), rom[i]);
    }
    core.set_pads(pads);
    std::copy(flags.begin(), flags.end(), hooks.flags.begin());
    if (hooks.end)
        hooks.end();
    c.exact = c.played && c.differ.empty();
    c.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    char secs[32];
    snprintf(secs, sizeof secs, "%.1f s", c.frames / 60.0);
    if (!c.played)
        c.text = "It couldn't be played: its saved state didn't load.";
    else if (c.exact)
        c.text = std::string("It plays back exactly (") + secs + ").";
    else
    {
        std::string which;
        for (size_t i = 0; i < c.differ.size(); i++)
            which += std::string(i == 0 ? "" : i + 1 == c.differ.size() ? " and " : ", ") + target_name(c.differ[i]);
        c.text = "It doesn't play back the same: after " + std::string(secs) + ", " + which +
                 (c.differ.size() == 1 ? " differs" : " differ") + " from the game now.";
    }
    return c;
}

// The file.

namespace
{

constexpr char kMagic[] = "S9XMOMENT\n";
constexpr uint32_t kVersion = 1;

struct Writer
{
    std::string out;
    void u8(uint8_t v) { out.push_back(char(v)); }
    void u16(uint16_t v)
    {
        u8(uint8_t(v));
        u8(uint8_t(v >> 8));
    }
    void u32(uint32_t v)
    {
        u16(uint16_t(v));
        u16(uint16_t(v >> 16));
    }
    void bytes(const void *p, size_t n) { out.append(static_cast<const char *>(p), n); }
    void writes(const std::vector<MomentWrite> &ws)
    {
        u32(uint32_t(ws.size()));
        for (auto &w : ws)
        {
            u8(uint8_t(w.target));
            u32(w.offset);
            u8(w.value);
        }
    }
};

struct Reader
{
    const std::string &in;
    size_t at = 0;
    bool ok = true;
    bool need(size_t n)
    {
        if (!ok || in.size() - at < n)
            ok = false;
        return ok;
    }
    uint8_t u8() { return need(1) ? uint8_t(in[at++]) : 0; }
    uint16_t u16()
    {
        uint16_t lo = u8();
        return uint16_t(lo | (u8() << 8));
    }
    uint32_t u32()
    {
        uint32_t lo = u16();
        return lo | (uint32_t(u16()) << 16);
    }
    std::string bytes(size_t n)
    {
        if (!need(n))
            return {};
        std::string s = in.substr(at, n);
        at += n;
        return s;
    }
    bool writes(std::vector<MomentWrite> &ws)
    {
        uint32_t n = u32();
        if (!need(size_t(n) * 6))
            return false;
        ws.resize(n);
        for (auto &w : ws)
        {
            uint8_t t = u8();
            if (t >= kTargetCount)
                return ok = false;
            w.target = Target(t);
            w.offset = u32();
            w.value = u8();
        }
        return ok;
    }
};

// zlib where the build has it (the app and the tests do); empty: stored as it is.
std::string pack(const std::string &raw)
{
#ifdef ZLIB
    uLongf n = compressBound(uLong(raw.size()));
    std::string out(n, '\0');
    if (compress2(reinterpret_cast<Bytef *>(out.data()), &n, reinterpret_cast<const Bytef *>(raw.data()), uLong(raw.size()),
                  6) != Z_OK)
        return {};
    out.resize(n);
    return out;
#else
    (void)raw;
    return {};
#endif
}

std::optional<std::string> unpack(const std::string &packed, uint32_t raw_size)
{
#ifdef ZLIB
    std::string out(raw_size, '\0');
    uLongf n = raw_size;
    if (uncompress(reinterpret_cast<Bytef *>(out.data()), &n, reinterpret_cast<const Bytef *>(packed.data()),
                   uLong(packed.size())) != Z_OK ||
        n != raw_size)
        return std::nullopt;
    return out;
#else
    (void)packed;
    (void)raw_size;
    return std::nullopt;
#endif
}

} // namespace

std::string encode_moment(const Moment &m)
{
    nlohmann::ordered_json h;
    h["format"] = kVersion;
    h["name"] = m.name;
    h["game"] = { { "title", m.game.title }, { "sha256", m.game.sha256 }, { "crc32", m.game.crc32 } };
    h["made_by"] = m.made_by;
    h["made_at"] = m.made_at;
    h["seconds"] = double(std::lround(m.seconds() * 10)) / 10;
    if (!m.recipe_name.empty())
        h["recipe_name"] = m.recipe_name;
    h["seed"] = m.seed;
    if (!m.starts.empty())
        h["starts"] = m.starts;
    if (!m.recipe.empty())
        h["recipe"] = m.recipe;
    std::string header = h.dump(2);

    Writer p; // what plays it: the keyframe, the ROM bytes, the frames
    p.u32(uint32_t(m.state.size()));
    p.bytes(m.state.data(), m.state.size());
    p.u32(uint32_t(m.rom.size()));
    for (auto &[o, v] : m.rom)
    {
        p.u32(o);
        p.u8(v);
    }
    p.u32(uint32_t(m.frames.size()));
    for (auto &f : m.frames)
    {
        for (uint16_t pad : f.pads)
            p.u16(pad);
        p.writes(f.before);
        p.writes(f.during);
    }
    p.writes(m.after);

    Writer w;
    w.bytes(kMagic, sizeof kMagic - 1);
    w.u32(kVersion);
    w.u32(uint32_t(header.size()));
    w.bytes(header.data(), header.size());
    std::string packed = pack(p.out);
    w.u8(packed.empty() ? 0 : 1); // 1: zlib (a save state is mostly zeros: 800 KB becomes about 100)
    w.u32(uint32_t(p.out.size()));
    const std::string &body = packed.empty() ? p.out : packed;
    w.u32(uint32_t(body.size()));
    w.bytes(body.data(), body.size());
    return std::move(w.out);
}

std::optional<Moment> decode_moment(const std::string &bytes, std::string &error)
{
    Reader r{ bytes };
    if (r.bytes(sizeof kMagic - 1) != std::string(kMagic, sizeof kMagic - 1))
    {
        error = "not a kept moment (.s9xmoment)";
        return std::nullopt;
    }
    if (uint32_t v = r.u32(); v != kVersion)
    {
        error = "a newer kind of kept moment (format " + std::to_string(v) + ")";
        return std::nullopt;
    }
    Moment m;
    std::string header = r.bytes(r.u32());
    auto h = nlohmann::json::parse(header, nullptr, false);
    if (!r.ok || h.is_discarded() || !h.is_object())
    {
        error = "its header is damaged";
        return std::nullopt;
    }
    auto str = [&](const nlohmann::json &j, const char *key) {
        auto it = j.find(key);
        return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    m.name = str(h, "name");
    if (auto g = h.find("game"); g != h.end() && g->is_object())
        m.game = { str(*g, "title"), str(*g, "sha256"), str(*g, "crc32") };
    m.made_by = str(h, "made_by");
    m.made_at = str(h, "made_at");
    m.starts = str(h, "starts");
    m.recipe = str(h, "recipe");
    m.recipe_name = str(h, "recipe_name");
    if (auto s = h.find("seed"); s != h.end() && s->is_number_unsigned())
        m.seed = s->get<uint64_t>();
    uint8_t packing = r.u8();
    uint32_t raw_size = r.u32();
    std::string stored = r.bytes(r.u32());
    if (!r.ok)
    {
        error = "it's cut short or damaged";
        return std::nullopt;
    }
    std::string body;
    if (packing == 0)
        body = std::move(stored);
    else if (packing == 1)
    {
        auto unpacked = unpack(stored, raw_size);
        if (!unpacked)
        {
            error = "it's damaged (it doesn't unpack)";
            return std::nullopt;
        }
        body = std::move(*unpacked);
    }
    else
    {
        error = "it's packed in a way this version doesn't know";
        return std::nullopt;
    }
    Reader b{ body };
    std::string state = b.bytes(b.u32());
    m.state.assign(state.begin(), state.end());
    uint32_t nrom = b.u32();
    if (b.need(size_t(nrom) * 5))
    {
        m.rom.resize(nrom);
        for (auto &[o, v] : m.rom)
        {
            o = b.u32();
            v = b.u8();
        }
    }
    uint32_t nframes = b.u32();
    if (b.need(size_t(nframes) * 24))
    {
        m.frames.resize(nframes);
        for (auto &f : m.frames)
        {
            for (auto &p : f.pads)
                p = b.u16();
            if (!b.writes(f.before) || !b.writes(f.during))
                break;
        }
    }
    b.writes(m.after);
    if (!b.ok || b.at != body.size() || r.at != bytes.size() || m.state.empty())
    {
        error = "it's cut short or damaged";
        return std::nullopt;
    }
    return m;
}

std::optional<MomentInfo> read_moment_info(const std::filesystem::path &path, std::string &error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        error = "it can't be read";
        return std::nullopt;
    }
    std::string start(sizeof kMagic - 1 + 8, '\0');
    in.read(start.data(), std::streamsize(start.size()));
    Reader r{ start };
    if (!in || r.bytes(sizeof kMagic - 1) != std::string(kMagic, sizeof kMagic - 1))
    {
        error = "not a kept moment (.s9xmoment)";
        return std::nullopt;
    }
    if (uint32_t v = r.u32(); v != kVersion)
    {
        error = "a newer kind of kept moment (format " + std::to_string(v) + ")";
        return std::nullopt;
    }
    uint32_t n = r.u32();
    if (n > (64u << 20))
    {
        error = "its header is damaged";
        return std::nullopt;
    }
    std::string header(n, '\0');
    in.read(header.data(), std::streamsize(n));
    auto h = nlohmann::json::parse(header, nullptr, false);
    if (!in || h.is_discarded() || !h.is_object())
    {
        error = "its header is damaged";
        return std::nullopt;
    }
    auto str = [&](const nlohmann::json &j, const char *key) {
        auto it = j.find(key);
        return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    MomentInfo info;
    info.path = path;
    info.name = str(h, "name");
    if (info.name.empty())
        info.name = path.stem().string();
    info.recipe_name = str(h, "recipe_name");
    info.made_by = str(h, "made_by");
    info.made_at = str(h, "made_at");
    info.starts = str(h, "starts");
    if (auto s = h.find("seconds"); s != h.end() && s->is_number())
        info.seconds = s->get<double>();
    if (auto g = h.find("game"); g != h.end() && g->is_object())
        info.game = { str(*g, "title"), str(*g, "sha256"), str(*g, "crc32") };
    return info;
}

} // namespace corrupt
