#pragma once
#include "corrupt/memory.h"

#include <array>
#include <vector>

// Plain byte buffers standing in for the emulator's memory.
struct FakeMemory : corrupt::MemoryAccess
{
    std::array<std::vector<uint8_t>, corrupt::kTargetCount> bytes;
    int writes = 0;

    FakeMemory()
    {
        for (auto t : corrupt::kAllTargets)
            bytes[size_t(t)].assign(corrupt::nominal_size(t), 0);
    }

    std::vector<uint8_t> &of(corrupt::Target t) { return bytes[size_t(t)]; }

    size_t size(corrupt::Target t) const override { return bytes[size_t(t)].size(); }
    uint8_t read(corrupt::Target t, uint32_t offset) const override { return bytes[size_t(t)].at(offset); }
    void write(corrupt::Target t, uint32_t offset, uint8_t value) override
    {
        bytes[size_t(t)].at(offset) = value;
        writes++;
    }
    const uint8_t *data(corrupt::Target t) const override { return bytes[size_t(t)].data(); }
};
