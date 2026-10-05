// Configuration structures plus parsers for the human-friendly CLI syntax.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "cachesim/types.hpp"

namespace cachesim {

class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct CacheConfig {
    std::string name = "L1";
    std::uint64_t sizeBytes = 32 * 1024;
    std::uint32_t associativity = 8;  // 1 = direct-mapped, lines = fully associative
    std::uint32_t blockBytes = 64;
    ReplacementKind replacement = ReplacementKind::LRU;
    WritePolicy writePolicy = WritePolicy::WriteBack;
    Cycles hitLatency = 4;

    std::uint64_t numLines() const { return sizeBytes / blockBytes; }
    std::uint64_t numSets() const { return numLines() / associativity; }

    // Throws ConfigError if the geometry is not realisable in hardware
    // (non power-of-two block or set count, size not divisible, ...).
    void validate() const;
    std::string organisation() const;  // "direct-mapped", "8-way", "fully-assoc"
};

enum class TracePattern { Sequential, Random, Strided, File };

struct TraceConfig {
    TracePattern pattern = TracePattern::Sequential;
    std::uint64_t numAccesses = 100000;
    Addr baseAddr = 0x10000000;
    std::uint64_t workingSetBytes = 256 * 1024;  // addresses wrap inside this region
    std::uint32_t elementBytes = 4;              // access size / step for sequential
    std::uint64_t strideBytes = 64;              // step for the strided pattern
    double writeRatio = 0.3;                     // probability an access is a store
    std::uint64_t seed = 42;
    std::string tracePath;  // for TracePattern::File
};

struct SimConfig {
    std::vector<CacheConfig> levels;  // index 0 is closest to the CPU
    Cycles memoryLatency = 100;
    TraceConfig trace;
    std::uint64_t seed = 42;  // seeds Random replacement
    std::string label;

    void validate() const;
    static SimConfig defaults();
};

// "32K" -> 32768, "2MB" -> 2097152, "64" -> 64. Binary (1024) multiples.
std::uint64_t parseSize(const std::string& text);

// "name=L1,size=32K,assoc=8,block=64,policy=lru,write=wb,hit=4"
// Any key may be omitted; assoc accepts "full" for fully associative.
CacheConfig parseLevelSpec(const std::string& spec);

ReplacementKind parseReplacement(const std::string& text);
WritePolicy parseWritePolicy(const std::string& text);
TracePattern parsePattern(const std::string& text);
std::string toString(TracePattern pattern);

}  // namespace cachesim
