#pragma once

#include <memory>
#include <string>

#include "cachesim/cache.hpp"
#include "cachesim/memory_level.hpp"

// A single cache in front of main memory, for focused unit tests.
struct SingleCache {
    explicit SingleCache(const std::string& spec, cachesim::Cycles memLatency = 100, std::uint64_t seed = 1)
        : memory(memLatency), cache(cachesim::parseLevelSpec(spec), &memory, seed) {}

    cachesim::Cycles read(cachesim::Addr a) { return cache.access(a, cachesim::AccessType::Read, 4); }
    cachesim::Cycles write(cachesim::Addr a) { return cache.access(a, cachesim::AccessType::Write, 4); }

    cachesim::MainMemory memory;
    cachesim::Cache cache;
};
