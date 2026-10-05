// Output formatting: human-readable table, CSV (one row per level) and JSON.
#pragma once

#include <ostream>

#include "cachesim/hierarchy.hpp"

namespace cachesim {

void writeText(std::ostream& out, const SimulationResult& result);
void writeCsv(std::ostream& out, const SimulationResult& result, bool header = true);
void writeJson(std::ostream& out, const SimulationResult& result);

}  // namespace cachesim
