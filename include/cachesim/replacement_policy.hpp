// Replacement policies decide which way of a full set is evicted on a miss.
//
// The cache tells the policy about every hit and every fill; the policy keeps
// whatever per-line metadata it needs. Invalid ways are always filled first by
// the cache itself, so victim() is only called on a full set.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "cachesim/types.hpp"

namespace cachesim {

class ReplacementPolicy {
public:
    virtual ~ReplacementPolicy() = default;

    virtual void onHit(std::size_t set, std::size_t way) = 0;
    virtual void onFill(std::size_t set, std::size_t way) = 0;
    virtual std::size_t victim(std::size_t set) = 0;
    virtual std::string name() const = 0;
};

// Least Recently Used: evicts the line whose last touch is oldest.
// Implemented with a global logical clock stamped into each line, which makes
// victim selection O(ways) and keeps the update on a hit O(1).
class LruPolicy final : public ReplacementPolicy {
public:
    LruPolicy(std::size_t sets, std::size_t ways);
    void onHit(std::size_t set, std::size_t way) override;
    void onFill(std::size_t set, std::size_t way) override;
    std::size_t victim(std::size_t set) override;
    std::string name() const override { return "LRU"; }

private:
    std::size_t ways_;
    std::uint64_t clock_ = 0;
    std::vector<std::uint64_t> stamp_;  // sets * ways, last-use time
};

// First In First Out: evicts the line that was brought in earliest.
// Hits do not change the order, which is exactly what separates it from LRU.
class FifoPolicy final : public ReplacementPolicy {
public:
    FifoPolicy(std::size_t sets, std::size_t ways);
    void onHit(std::size_t, std::size_t) override {}
    void onFill(std::size_t set, std::size_t way) override;
    std::size_t victim(std::size_t set) override;
    std::string name() const override { return "FIFO"; }

private:
    std::size_t ways_;
    std::uint64_t clock_ = 0;
    std::vector<std::uint64_t> inserted_;  // sets * ways, fill time
};

// Random: evicts a uniformly chosen way. Seeded so runs are reproducible.
class RandomPolicy final : public ReplacementPolicy {
public:
    RandomPolicy(std::size_t ways, std::uint64_t seed);
    void onHit(std::size_t, std::size_t) override {}
    void onFill(std::size_t, std::size_t) override {}
    std::size_t victim(std::size_t set) override;
    std::string name() const override { return "Random"; }

private:
    std::mt19937_64 rng_;
    std::uniform_int_distribution<std::size_t> dist_;
};

std::unique_ptr<ReplacementPolicy> makePolicy(ReplacementKind kind, std::size_t sets,
                                              std::size_t ways, std::uint64_t seed);

}  // namespace cachesim
