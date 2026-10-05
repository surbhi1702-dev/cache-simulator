// Synthetic memory-access trace generators and a trace file reader/writer.
//
// Trace file format, one access per line ('#' starts a comment):
//     R 0x7ffd1000        load  (also: "r", "L", "0")
//     W 0x7ffd1004 8      store of 8 bytes (also: "w", "S", "1")
// Addresses may be hex (0x...) or decimal; the size column is optional.
#pragma once

#include <string>
#include <vector>

#include "cachesim/config.hpp"
#include "cachesim/types.hpp"

namespace cachesim {

std::vector<MemAccess> generateTrace(const TraceConfig& config);

std::vector<MemAccess> loadTrace(const std::string& path, std::uint32_t defaultSize = 4);
void saveTrace(const std::string& path, const std::vector<MemAccess>& trace);

}  // namespace cachesim
