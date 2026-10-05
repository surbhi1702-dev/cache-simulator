// A single set-associative cache level.
//
// Address split (block = 2^b bytes, sets = 2^s):
//
//     | tag ............ | index (s bits) | offset (b bits) |
//
// Direct-mapped is associativity 1, fully associative is a single set.
// The cache is non-inclusive/non-exclusive with respect to the levels below.
#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cachesim/config.hpp"
#include "cachesim/memory_level.hpp"
#include "cachesim/replacement_policy.hpp"

namespace cachesim {

struct CacheStats {
    // Demand traffic (CPU requests, or block fills requested by the level above).
    std::uint64_t reads = 0;
    std::uint64_t writes = 0;
    std::uint64_t readHits = 0;
    std::uint64_t writeHits = 0;
    std::uint64_t readMisses = 0;
    std::uint64_t writeMisses = 0;

    // 3C classification of demand misses (Hill & Smith).
    std::uint64_t compulsoryMisses = 0;
    std::uint64_t capacityMisses = 0;
    std::uint64_t conflictMisses = 0;

    std::uint64_t evictions = 0;      // valid lines replaced
    std::uint64_t writebacks = 0;     // dirty lines written to the next level
    std::uint64_t writeThroughs = 0;  // stores forwarded by write-through

    // Writes received from the level above (writebacks / write-through stores).
    std::uint64_t writesFromAbove = 0;
    std::uint64_t writesFromAboveHits = 0;

    std::uint64_t accesses() const { return reads + writes; }
    std::uint64_t hits() const { return readHits + writeHits; }
    std::uint64_t misses() const { return readMisses + writeMisses; }
    double hitRate() const { return accesses() ? double(hits()) / double(accesses()) : 0.0; }
    double missRate() const { return accesses() ? double(misses()) / double(accesses()) : 0.0; }
};

struct AddressParts {
    Addr tag;
    std::uint64_t index;
    std::uint64_t offset;
};

class Cache final : public MemoryLevel {
public:
    Cache(const CacheConfig& config, MemoryLevel* next, std::uint64_t seed);

    Cycles access(Addr addr, AccessType type, std::uint32_t bytes) override;
    void writeDown(Addr addr, std::uint32_t bytes) override;
    const std::string& name() const override { return config_.name; }

    AddressParts decompose(Addr addr) const;
    Addr blockAddress(Addr addr) const { return addr & ~Addr(config_.blockBytes - 1); }
    bool contains(Addr addr) const;
    std::uint64_t dirtyLines() const;

    const CacheConfig& config() const { return config_; }
    const CacheStats& stats() const { return stats_; }
    unsigned offsetBits() const { return offsetBits_; }
    unsigned indexBits() const { return indexBits_; }

private:
    struct Line {
        Addr tag = 0;
        bool valid = false;
        bool dirty = false;
    };

    Line& line(std::uint64_t set, std::uint64_t way) { return lines_[set * ways_ + way]; }
    const Line& line(std::uint64_t set, std::uint64_t way) const { return lines_[set * ways_ + way]; }

    // Returns the way holding `tag` in `set`, or -1.
    long findWay(std::uint64_t set, Addr tag) const;
    // Picks a way for a new block (invalid first, else the policy's victim),
    // writing back a dirty victim. Returns the way, now free.
    std::uint64_t allocate(std::uint64_t set);
    Addr rebuildAddress(Addr tag, std::uint64_t set) const;

    // Shadow fully-associative LRU cache with the same number of lines, used
    // to split non-compulsory misses into capacity vs conflict.
    bool shadowLookup(Addr block, bool allocate);
    void classifyMiss(Addr block, bool shadowHit);

    CacheConfig config_;
    MemoryLevel* next_;
    std::unique_ptr<ReplacementPolicy> policy_;
    std::uint64_t sets_;
    std::uint64_t ways_;
    unsigned offsetBits_;
    unsigned indexBits_;
    std::vector<Line> lines_;
    CacheStats stats_;

    std::unordered_set<Addr> everSeen_;
    std::list<Addr> shadowLru_;  // front = most recently used
    std::unordered_map<Addr, std::list<Addr>::iterator> shadowMap_;
};

}  // namespace cachesim
