#include "cachesim/replacement_policy.hpp"

#include <limits>

namespace cachesim {

LruPolicy::LruPolicy(std::size_t sets, std::size_t ways) : ways_(ways), stamp_(sets * ways, 0) {}

void LruPolicy::onHit(std::size_t set, std::size_t way) { stamp_[set * ways_ + way] = ++clock_; }

void LruPolicy::onFill(std::size_t set, std::size_t way) { stamp_[set * ways_ + way] = ++clock_; }

std::size_t LruPolicy::victim(std::size_t set) {
    std::size_t best = 0;
    std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t w = 0; w < ways_; ++w) {
        if (stamp_[set * ways_ + w] < oldest) {
            oldest = stamp_[set * ways_ + w];
            best = w;
        }
    }
    return best;
}

FifoPolicy::FifoPolicy(std::size_t sets, std::size_t ways) : ways_(ways), inserted_(sets * ways, 0) {}

void FifoPolicy::onFill(std::size_t set, std::size_t way) { inserted_[set * ways_ + way] = ++clock_; }

std::size_t FifoPolicy::victim(std::size_t set) {
    std::size_t best = 0;
    std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t w = 0; w < ways_; ++w) {
        if (inserted_[set * ways_ + w] < oldest) {
            oldest = inserted_[set * ways_ + w];
            best = w;
        }
    }
    return best;
}

RandomPolicy::RandomPolicy(std::size_t ways, std::uint64_t seed) : rng_(seed), dist_(0, ways - 1) {}

std::size_t RandomPolicy::victim(std::size_t) { return dist_(rng_); }

std::unique_ptr<ReplacementPolicy> makePolicy(ReplacementKind kind, std::size_t sets,
                                              std::size_t ways, std::uint64_t seed) {
    switch (kind) {
        case ReplacementKind::LRU: return std::make_unique<LruPolicy>(sets, ways);
        case ReplacementKind::FIFO: return std::make_unique<FifoPolicy>(sets, ways);
        case ReplacementKind::Random: return std::make_unique<RandomPolicy>(ways, seed);
    }
    return nullptr;
}

}  // namespace cachesim
