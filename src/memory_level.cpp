#include "cachesim/memory_level.hpp"

namespace cachesim {

Cycles MainMemory::access(Addr, AccessType type, std::uint32_t bytes) {
    if (type == AccessType::Read) {
        ++stats_.reads;
        stats_.bytesRead += bytes;
    } else {
        ++stats_.writes;
        stats_.bytesWritten += bytes;
    }
    return latency_;
}

void MainMemory::writeDown(Addr, std::uint32_t bytes) {
    ++stats_.writes;
    stats_.bytesWritten += bytes;
}

}  // namespace cachesim
