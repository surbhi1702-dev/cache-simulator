// Wires CPU -> L1 -> ... -> Ln -> main memory and collects results.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "cachesim/cache.hpp"
#include "cachesim/config.hpp"
#include "cachesim/memory_level.hpp"

namespace cachesim {

struct LevelResult {
    CacheConfig config;
    CacheStats stats;
    std::uint64_t dirtyLinesAtEnd = 0;
    double globalMissRate = 0.0;  // misses at this level / CPU accesses
};

struct SimulationResult {
    std::string label;
    TraceConfig trace;
    Cycles memoryLatency = 0;
    std::uint64_t cpuAccesses = 0;
    std::uint64_t cpuReads = 0;
    std::uint64_t cpuWrites = 0;
    std::uint64_t uniqueBlocks = 0;  // distinct L1-sized blocks touched
    Cycles totalCycles = 0;
    double amat = 0.0;         // measured: totalCycles / cpuAccesses
    double amatFormula = 0.0;  // analytic: h1 + m1*(h2 + m2*(... + Tmem))
    std::vector<LevelResult> levels;
    MemoryStats memory;
};

class Hierarchy {
public:
    explicit Hierarchy(const SimConfig& config);

    Cycles access(const MemAccess& access);
    void run(const std::vector<MemAccess>& trace);
    SimulationResult result() const;

    Cache& level(std::size_t i) { return *levels_.at(i); }
    std::size_t depth() const { return levels_.size(); }
    const MainMemory& memory() const { return *memory_; }

private:
    SimConfig config_;
    std::unique_ptr<MainMemory> memory_;
    std::vector<std::unique_ptr<Cache>> levels_;  // [0] = L1
    std::uint64_t cpuAccesses_ = 0;
    std::uint64_t cpuReads_ = 0;
    std::uint64_t cpuWrites_ = 0;
    Cycles totalCycles_ = 0;
    std::unordered_set<Addr> uniqueBlocks_;
};

// Classic recursive AMAT from per-level local miss rates.
double analyticAmat(const std::vector<LevelResult>& levels, Cycles memoryLatency);

}  // namespace cachesim
