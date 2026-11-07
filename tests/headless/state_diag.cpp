// Diagnostics for a playback that drifts (moved out of corrupt-smoke's --keep for the explorer, #86).
#include "state_diag.h"

#include "snes9x.h"
#include "memmap.h"
#include "cpuexec.h"
#include "ppu.h"
#include "dma.h"
#include "gfx.h"
#include "apu/bapu/snes/snes.hpp"

#include <cstdio>
#include <string>

using corrupt::Target;

namespace headless
{

// Every memory in one number, for finding where a playback drifts.
uint64_t state_hash(const corrupt::MemoryAccess &memory)
{
    uint64_t h = 1469598103934665603ull;
    for (auto t : { Target::WRAM, Target::VRAM, Target::OAM, Target::CGRAM, Target::ARAM, Target::ROM })
    {
        const uint8_t *d = memory.data(t);
        for (size_t i = 0, n = memory.size(t); i < n; i++)
            h = (h ^ (d ? d[i] : memory.read(t, uint32_t(i)))) * 1099511628211ull;
    }
    return h;
}

// The core's globals as bytes, for finding state a save doesn't hold (diagnostics).
std::map<std::string, std::vector<uint8_t>> core_globals()
{
    std::map<std::string, std::vector<uint8_t>> g;
    auto add = [&](const char *name, const void *p, size_t n) {
        g[name].assign(static_cast<const uint8_t *>(p), static_cast<const uint8_t *>(p) + n);
    };
    add("CPU", &CPU, sizeof CPU);
    add("ICPU", &ICPU, sizeof ICPU);
    add("Registers", &Registers, sizeof Registers);
    add("Timings", &Timings, sizeof Timings);
    add("PPU", &PPU, sizeof PPU);
    add("IPPU", &IPPU, sizeof IPPU);
    add("DMA", DMA, sizeof DMA);
    add("Settings", &Settings, sizeof Settings);
    add("FillRAM", Memory.FillRAM, 0x8000);
    add("smp", &SNES::smp, sizeof SNES::smp);
    add("dsp", &SNES::dsp, sizeof SNES::dsp);
    add("Memory", &Memory, sizeof Memory);
    add("GFX", &GFX, sizeof GFX);
    add("BG", &BG, sizeof BG);
    add("OpenBus", &OpenBus, 1);
    return g;
}

void print_globals_diff(const std::map<std::string, std::vector<uint8_t>> &a,
                        const std::map<std::string, std::vector<uint8_t>> &b)
{
    for (auto &[name, va] : a)
    {
        auto &vb = b.at(name);
        std::string where;
        int n = 0;
        for (size_t i = 0; i < va.size(); i++)
            if (va[i] != vb[i])
            {
                if (n++ < 12)
                {
                    char buf[48];
                    snprintf(buf, sizeof buf, " +%zu:%02X/%02X", i, va[i], vb[i]);
                    where += buf;
                }
            }
        if (n)
            printf("        %s: %d bytes differ:%s\n", name.c_str(), n, where.c_str());
    }
}

// Which blocks of two saved states differ ("CPU", "SND", ...), and where in them.
void print_state_diff(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
    // A freeze: "#!s9xsnp:0011\n", then blocks "NAM:123456:" followed by that many bytes.
    auto blocks = [](const std::vector<uint8_t> &s) {
        std::map<std::string, std::pair<size_t, size_t>> out; // name -> (start, length)
        size_t at = 14;
        while (at + 11 <= s.size())
        {
            std::string name(s.begin() + at, s.begin() + at + 3);
            size_t len = std::stoul(std::string(s.begin() + at + 4, s.begin() + at + 10));
            out[name] = { at + 11, len };
            at += 11 + len;
        }
        return out;
    };
    auto ba = blocks(a), bb = blocks(b);
    for (auto &[name, ra] : ba)
    {
        auto it = bb.find(name);
        if (it == bb.end() || it->second.second != ra.second)
        {
            printf("        block %s: missing or another size\n", name.c_str());
            continue;
        }
        size_t first = SIZE_MAX, n = 0;
        for (size_t i = 0; i < ra.second; i++)
            if (a[ra.first + i] != b[it->second.first + i])
            {
                if (first == SIZE_MAX)
                    first = i;
                n++;
            }
        if (n)
            printf("        block %s: %zu of %zu bytes differ, the first at %zu\n", name.c_str(), n, ra.second, first);
    }
}

std::string core_where()
{
    char buf[160];
    snprintf(buf, sizeof buf, "V %d, EndY %d, StartY %d, line %d, previous line %d, RTO %02X, drawing %d", int(CPU.V_Counter),
             int(GFX.EndY), int(GFX.StartY), int(IPPU.CurrentLine), int(IPPU.PreviousLine), int(PPU.RangeTimeOver),
             int(IPPU.RenderThisFrame));
    return buf;
}

} // namespace headless
