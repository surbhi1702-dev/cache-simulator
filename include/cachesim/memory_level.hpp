// A MemoryLevel is anything a cache can miss into: another cache or DRAM.
#pragma once

#include <cstdint>
#include <string>

#include "cachesim/types.hpp"

namespace cachesim {

class MemoryLevel {
public:
    virtual ~MemoryLevel() = default;

    // Demand access on the critical path. Returns the cycles until the data is
    // available to the requester. `bytes` is the request size (a word from the
    // CPU, a whole block from an upper cache) and is used for traffic counts.
    virtual Cycles access(Addr addr, AccessType type, std::uint32_t bytes) = 0;

    // A write pushed down from the level above: a dirty-block writeback
    // (bytes == upper block size) or a write-through store (bytes == word).
    // Modelled as going through a write buffer, so it is off the critical path
    // and returns no latency.
    virtual void writeDown(Addr addr, std::uint32_t bytes) = 0;

    virtual const std::string& name() const = 0;
};

struct MemoryStats {
    std::uint64_t reads = 0;
    std::uint64_t writes = 0;
    std::uint64_t bytesRead = 0;
    std::uint64_t bytesWritten = 0;
};

// Main memory (DRAM): always hits, fixed access latency.
class MainMemory final : public MemoryLevel {
public:
    explicit MainMemory(Cycles latency) : latency_(latency) {}

    Cycles access(Addr addr, AccessType type, std::uint32_t bytes) override;
    void writeDown(Addr addr, std::uint32_t bytes) override;
    const std::string& name() const override { return name_; }

    Cycles latency() const { return latency_; }
    const MemoryStats& stats() const { return stats_; }

private:
    std::string name_ = "MEM";
    Cycles latency_;
    MemoryStats stats_;
};

}  // namespace cachesim
