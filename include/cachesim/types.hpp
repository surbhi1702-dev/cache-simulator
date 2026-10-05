// Core vocabulary types shared by every component of the simulator.
#pragma once

#include <cstdint>
#include <string>

namespace cachesim {

using Addr = std::uint64_t;    // physical byte address
using Cycles = std::uint64_t;  // latency in CPU cycles

enum class AccessType : std::uint8_t { Read, Write };

// One memory reference issued by the CPU (a load or a store of `size` bytes).
struct MemAccess {
    Addr addr = 0;
    AccessType type = AccessType::Read;
    std::uint32_t size = 4;
};

enum class ReplacementKind { LRU, FIFO, Random };

// WriteBack is always paired with write-allocate, WriteThrough with
// no-write-allocate: the two textbook combinations (Hennessy & Patterson).
enum class WritePolicy { WriteBack, WriteThrough };

std::string toString(ReplacementKind kind);
std::string toString(WritePolicy policy);
std::string toString(AccessType type);

}  // namespace cachesim
