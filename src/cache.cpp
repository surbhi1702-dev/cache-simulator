#include "cachesim/cache.hpp"

namespace cachesim {

namespace {

unsigned log2u(std::uint64_t x) {
    unsigned n = 0;
    while (x > 1) {
        x >>= 1;
        ++n;
    }
    return n;
}

}  // namespace

Cache::Cache(const CacheConfig& config, MemoryLevel* next, std::uint64_t seed)
    : config_(config), next_(next) {
    config_.validate();
    sets_ = config_.numSets();
    ways_ = config_.associativity;
    offsetBits_ = log2u(config_.blockBytes);
    indexBits_ = log2u(sets_);
    lines_.assign(sets_ * ways_, Line{});
    policy_ = makePolicy(config_.replacement, sets_, ways_, seed);
}

AddressParts Cache::decompose(Addr addr) const {
    AddressParts p;
    p.offset = addr & (Addr(config_.blockBytes) - 1);
    p.index = (addr >> offsetBits_) & (sets_ - 1);
    p.tag = addr >> (offsetBits_ + indexBits_);
    return p;
}

Addr Cache::rebuildAddress(Addr tag, std::uint64_t set) const {
    return (tag << (offsetBits_ + indexBits_)) | (Addr(set) << offsetBits_);
}

long Cache::findWay(std::uint64_t set, Addr tag) const {
    for (std::uint64_t w = 0; w < ways_; ++w) {
        const Line& l = line(set, w);
        if (l.valid && l.tag == tag) return static_cast<long>(w);
    }
    return -1;
}

bool Cache::contains(Addr addr) const {
    auto p = decompose(addr);
    return findWay(p.index, p.tag) >= 0;
}

std::uint64_t Cache::dirtyLines() const {
    std::uint64_t n = 0;
    for (const auto& l : lines_) n += (l.valid && l.dirty) ? 1 : 0;
    return n;
}

std::uint64_t Cache::allocate(std::uint64_t set) {
    for (std::uint64_t w = 0; w < ways_; ++w)
        if (!line(set, w).valid) return w;

    std::uint64_t victim = policy_->victim(set);
    Line& v = line(set, victim);
    ++stats_.evictions;
    if (v.dirty) {
        ++stats_.writebacks;
        next_->writeDown(rebuildAddress(v.tag, set), config_.blockBytes);
    }
    v = Line{};
    return victim;
}

bool Cache::shadowLookup(Addr block, bool allocate) {
    auto it = shadowMap_.find(block);
    if (it != shadowMap_.end()) {
        shadowLru_.splice(shadowLru_.begin(), shadowLru_, it->second);
        return true;
    }
    if (allocate) {
        shadowLru_.push_front(block);
        shadowMap_[block] = shadowLru_.begin();
        if (shadowLru_.size() > config_.numLines()) {
            shadowMap_.erase(shadowLru_.back());
            shadowLru_.pop_back();
        }
    }
    return false;
}

Cycles Cache::access(Addr addr, AccessType type, std::uint32_t bytes) {
    const bool isWrite = type == AccessType::Write;
    const bool writeBack = config_.writePolicy == WritePolicy::WriteBack;
    isWrite ? ++stats_.writes : ++stats_.reads;

    const Addr block = blockAddress(addr);
    const AddressParts p = decompose(addr);
    const long way = findWay(p.index, p.tag);
    const bool hit = way >= 0;

    // Write-through is paired with no-write-allocate: a store miss does not
    // bring the block in, so neither the real nor the shadow cache allocates.
    const bool allocates = hit || !isWrite || writeBack;
    const bool firstTouch = everSeen_.insert(block).second;
    const bool shadowHit = shadowLookup(block, allocates);

    if (hit) {
        isWrite ? ++stats_.writeHits : ++stats_.readHits;
        policy_->onHit(p.index, static_cast<std::size_t>(way));
        if (isWrite) {
            if (writeBack) {
                line(p.index, way).dirty = true;
            } else {
                ++stats_.writeThroughs;
                next_->writeDown(addr, bytes);
            }
        }
        return config_.hitLatency;
    }

    isWrite ? ++stats_.writeMisses : ++stats_.readMisses;
    if (firstTouch) ++stats_.compulsoryMisses;
    else if (shadowHit) ++stats_.conflictMisses;
    else ++stats_.capacityMisses;

    if (!allocates) {
        // Write-through, no-write-allocate store miss: forward the word and
        // let the write buffer absorb the latency.
        ++stats_.writeThroughs;
        next_->writeDown(addr, bytes);
        return config_.hitLatency;
    }

    // Miss penalty: look up here, then fetch the whole block from below.
    const Cycles latency = config_.hitLatency + next_->access(block, AccessType::Read, config_.blockBytes);
    const std::uint64_t w = allocate(p.index);
    Line& l = line(p.index, w);
    l.valid = true;
    l.tag = p.tag;
    l.dirty = isWrite;  // only reachable for writes when write-back
    policy_->onFill(p.index, w);
    if (isWrite && !writeBack) {
        ++stats_.writeThroughs;
        next_->writeDown(addr, bytes);
    }
    return latency;
}

void Cache::writeDown(Addr addr, std::uint32_t bytes) {
    ++stats_.writesFromAbove;
    const bool writeBack = config_.writePolicy == WritePolicy::WriteBack;
    const Addr block = blockAddress(addr);
    const AddressParts p = decompose(addr);
    const long way = findWay(p.index, p.tag);

    if (way >= 0) {
        ++stats_.writesFromAboveHits;
        policy_->onHit(p.index, static_cast<std::size_t>(way));
        shadowLookup(block, true);
        if (writeBack) {
            line(p.index, way).dirty = true;
        } else {
            ++stats_.writeThroughs;
            next_->writeDown(addr, bytes);
        }
        return;
    }

    if (!writeBack) {
        ++stats_.writeThroughs;
        next_->writeDown(addr, bytes);
        return;
    }

    // Write-back + write-allocate. A full-block writeback overwrites every
    // byte, so there is nothing to fetch; a partial store must fetch the rest.
    if (bytes < config_.blockBytes) next_->access(block, AccessType::Read, config_.blockBytes);
    everSeen_.insert(block);
    shadowLookup(block, true);
    const std::uint64_t w = allocate(p.index);
    Line& l = line(p.index, w);
    l.valid = true;
    l.tag = p.tag;
    l.dirty = true;
    policy_->onFill(p.index, w);
}

}  // namespace cachesim
