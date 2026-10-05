#include "cachesim/hierarchy.hpp"

namespace cachesim {

Hierarchy::Hierarchy(const SimConfig& config) : config_(config) {
    config_.validate();
    memory_ = std::make_unique<MainMemory>(config_.memoryLatency);

    // Build bottom-up so each cache can point at the level below it.
    std::vector<std::unique_ptr<Cache>> reversed;
    MemoryLevel* below = memory_.get();
    for (std::size_t i = config_.levels.size(); i-- > 0;) {
        auto cache = std::make_unique<Cache>(config_.levels[i], below, config_.seed + i);
        below = cache.get();
        reversed.push_back(std::move(cache));
    }
    for (auto it = reversed.rbegin(); it != reversed.rend(); ++it) levels_.push_back(std::move(*it));
}

Cycles Hierarchy::access(const MemAccess& a) {
    ++cpuAccesses_;
    a.type == AccessType::Read ? ++cpuReads_ : ++cpuWrites_;
    uniqueBlocks_.insert(levels_.front()->blockAddress(a.addr));
    const Cycles c = levels_.front()->access(a.addr, a.type, a.size);
    totalCycles_ += c;
    return c;
}

void Hierarchy::run(const std::vector<MemAccess>& trace) {
    for (const auto& a : trace) access(a);
}

double analyticAmat(const std::vector<LevelResult>& levels, Cycles memoryLatency) {
    // Evaluate from the bottom up: penalty(n) = Tmem, AMAT_i = h_i + m_i * AMAT_{i+1}.
    double t = static_cast<double>(memoryLatency);
    for (auto it = levels.rbegin(); it != levels.rend(); ++it)
        t = static_cast<double>(it->config.hitLatency) + it->stats.missRate() * t;
    return t;
}

SimulationResult Hierarchy::result() const {
    SimulationResult r;
    r.label = config_.label;
    r.trace = config_.trace;
    r.memoryLatency = config_.memoryLatency;
    r.cpuAccesses = cpuAccesses_;
    r.cpuReads = cpuReads_;
    r.cpuWrites = cpuWrites_;
    r.uniqueBlocks = uniqueBlocks_.size();
    r.totalCycles = totalCycles_;
    r.amat = cpuAccesses_ ? double(totalCycles_) / double(cpuAccesses_) : 0.0;
    for (const auto& c : levels_) {
        LevelResult lr;
        lr.config = c->config();
        lr.stats = c->stats();
        lr.dirtyLinesAtEnd = c->dirtyLines();
        lr.globalMissRate = cpuAccesses_ ? double(lr.stats.misses()) / double(cpuAccesses_) : 0.0;
        r.levels.push_back(lr);
    }
    r.amatFormula = analyticAmat(r.levels, config_.memoryLatency);
    r.memory = memory_->stats();
    return r;
}

}  // namespace cachesim
