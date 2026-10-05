#include "cachesim/report.hpp"

#include <iomanip>
#include <sstream>

namespace cachesim {

namespace {

std::string humanSize(std::uint64_t bytes) {
    std::ostringstream s;
    if (bytes >= (1ULL << 20) && bytes % (1ULL << 20) == 0) s << (bytes >> 20) << "MB";
    else if (bytes >= 1024 && bytes % 1024 == 0) s << (bytes >> 10) << "KB";
    else s << bytes << "B";
    return s.str();
}

std::string jsonEscape(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\n': o << "\\n"; break;
            case '\t': o << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c);
                else o << c;
        }
    }
    return o.str();
}

std::string csvField(const std::string& s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) out += (c == '"') ? std::string("\"\"") : std::string(1, c);
    return out + "\"";
}

}  // namespace

void writeText(std::ostream& out, const SimulationResult& r) {
    out << std::fixed;
    out << "=== Cache Hierarchy Simulation" << (r.label.empty() ? "" : " [" + r.label + "]") << " ===\n";
    out << "Trace     : " << toString(r.trace.pattern);
    if (r.trace.pattern == TracePattern::File) out << " (" << r.trace.tracePath << ")";
    else
        out << ", working set " << humanSize(r.trace.workingSetBytes) << ", element " << r.trace.elementBytes
            << "B" << (r.trace.pattern == TracePattern::Strided ? ", stride " + std::to_string(r.trace.strideBytes) + "B" : "");
    out << "\nAccesses  : " << r.cpuAccesses << " (" << r.cpuReads << " reads, " << r.cpuWrites
        << " writes), " << r.uniqueBlocks << " unique blocks\n\n";

    out << std::left << std::setw(5) << "Level" << std::right << std::setw(8) << "Size" << std::setw(15)
        << "Organisation" << std::setw(7) << "Block" << std::setw(8) << "Repl" << std::setw(15) << "Write"
        << std::setw(6) << "Hit" << '\n';
    for (const auto& l : r.levels) {
        const auto& c = l.config;
        out << std::left << std::setw(5) << c.name << std::right << std::setw(8) << humanSize(c.sizeBytes)
            << std::setw(15) << c.organisation() << std::setw(6) << c.blockBytes << "B" << std::setw(8)
            << toString(c.replacement) << std::setw(15) << toString(c.writePolicy) << std::setw(6)
            << c.hitLatency << '\n';
    }
    out << std::left << std::setw(5) << "MEM" << std::right << std::setw(59) << r.memoryLatency << "\n\n";

    out << std::left << std::setw(5) << "Level" << std::right << std::setw(11) << "Accesses" << std::setw(11)
        << "Hits" << std::setw(11) << "Misses" << std::setw(9) << "Hit%" << std::setw(10) << "Global%"
        << std::setw(11) << "Compuls." << std::setw(10) << "Capacity" << std::setw(10) << "Conflict"
        << std::setw(10) << "Evict" << std::setw(10) << "WrBack" << '\n';
    for (const auto& l : r.levels) {
        const auto& s = l.stats;
        out << std::left << std::setw(5) << l.config.name << std::right << std::setw(11) << s.accesses()
            << std::setw(11) << s.hits() << std::setw(11) << s.misses() << std::setw(9) << std::setprecision(2)
            << 100.0 * s.hitRate() << std::setw(10) << 100.0 * l.globalMissRate << std::setw(11)
            << s.compulsoryMisses << std::setw(10) << s.capacityMisses << std::setw(10) << s.conflictMisses
            << std::setw(10) << s.evictions << std::setw(10) << s.writebacks << '\n';
    }
    out << "\nMain memory: " << r.memory.reads << " reads (" << humanSize(r.memory.bytesRead) << "), "
        << r.memory.writes << " writes (" << humanSize(r.memory.bytesWritten) << ")\n";
    out << std::setprecision(3) << "Total cycles      : " << r.totalCycles << '\n'
        << "AMAT (measured)   : " << r.amat << " cycles\n"
        << "AMAT (formula)    : " << r.amatFormula << " cycles  = h1 + m1*(h2 + m2*(... + Tmem))\n";
}

void writeCsv(std::ostream& out, const SimulationResult& r, bool header) {
    if (header) {
        out << "label,pattern,accesses,level,size_bytes,assoc,organisation,block_bytes,replacement,"
               "write_policy,hit_latency,level_accesses,hits,misses,hit_rate,miss_rate,global_miss_rate,"
               "compulsory,capacity,conflict,evictions,writebacks,write_throughs,mem_reads,mem_writes,"
               "mem_bytes_read,mem_bytes_written,amat,amat_formula\n";
    }
    for (const auto& l : r.levels) {
        const auto& c = l.config;
        const auto& s = l.stats;
        out << csvField(r.label) << ',' << toString(r.trace.pattern) << ',' << r.cpuAccesses << ','
            << csvField(c.name) << ',' << c.sizeBytes << ',' << c.associativity << ',' << c.organisation() << ','
            << c.blockBytes << ',' << toString(c.replacement) << ',' << toString(c.writePolicy) << ','
            << c.hitLatency << ',' << s.accesses() << ',' << s.hits() << ',' << s.misses() << ','
            << std::setprecision(6) << std::fixed << s.hitRate() << ',' << s.missRate() << ','
            << l.globalMissRate << ',' << s.compulsoryMisses << ',' << s.capacityMisses << ','
            << s.conflictMisses << ',' << s.evictions << ',' << s.writebacks << ',' << s.writeThroughs << ','
            << r.memory.reads << ',' << r.memory.writes << ',' << r.memory.bytesRead << ','
            << r.memory.bytesWritten << ',' << r.amat << ',' << r.amatFormula << '\n';
    }
}

void writeJson(std::ostream& out, const SimulationResult& r) {
    out << std::setprecision(6) << std::fixed;
    out << "{\n";
    out << "  \"label\": \"" << jsonEscape(r.label) << "\",\n";
    out << "  \"trace\": {\"pattern\": \"" << toString(r.trace.pattern) << "\", \"accesses\": " << r.cpuAccesses
        << ", \"reads\": " << r.cpuReads << ", \"writes\": " << r.cpuWrites
        << ", \"unique_blocks\": " << r.uniqueBlocks << ", \"working_set_bytes\": " << r.trace.workingSetBytes
        << ", \"element_bytes\": " << r.trace.elementBytes << ", \"stride_bytes\": " << r.trace.strideBytes
        << ", \"write_ratio\": " << r.trace.writeRatio << ", \"seed\": " << r.trace.seed
        << ", \"path\": \"" << jsonEscape(r.trace.tracePath) << "\"},\n";
    out << "  \"levels\": [\n";
    for (std::size_t i = 0; i < r.levels.size(); ++i) {
        const auto& c = r.levels[i].config;
        const auto& s = r.levels[i].stats;
        out << "    {\"name\": \"" << jsonEscape(c.name) << "\", \"size_bytes\": " << c.sizeBytes
            << ", \"assoc\": " << c.associativity << ", \"organisation\": \"" << c.organisation()
            << "\", \"sets\": " << c.numSets() << ", \"block_bytes\": " << c.blockBytes
            << ", \"replacement\": \"" << toString(c.replacement) << "\", \"write_policy\": \""
            << toString(c.writePolicy) << "\", \"hit_latency\": " << c.hitLatency
            << ",\n     \"accesses\": " << s.accesses() << ", \"reads\": " << s.reads
            << ", \"writes\": " << s.writes << ", \"hits\": " << s.hits() << ", \"misses\": " << s.misses()
            << ", \"read_hits\": " << s.readHits << ", \"write_hits\": " << s.writeHits
            << ", \"hit_rate\": " << s.hitRate() << ", \"miss_rate\": " << s.missRate()
            << ", \"global_miss_rate\": " << r.levels[i].globalMissRate
            << ",\n     \"compulsory\": " << s.compulsoryMisses << ", \"capacity\": " << s.capacityMisses
            << ", \"conflict\": " << s.conflictMisses << ", \"evictions\": " << s.evictions
            << ", \"writebacks\": " << s.writebacks << ", \"write_throughs\": " << s.writeThroughs
            << ", \"writes_from_above\": " << s.writesFromAbove
            << ", \"dirty_lines_at_end\": " << r.levels[i].dirtyLinesAtEnd << "}"
            << (i + 1 < r.levels.size() ? "," : "") << "\n";
    }
    out << "  ],\n";
    out << "  \"memory\": {\"latency\": " << r.memoryLatency << ", \"reads\": " << r.memory.reads
        << ", \"writes\": " << r.memory.writes << ", \"bytes_read\": " << r.memory.bytesRead
        << ", \"bytes_written\": " << r.memory.bytesWritten << "},\n";
    out << "  \"total_cycles\": " << r.totalCycles << ",\n";
    out << "  \"amat\": " << r.amat << ",\n";
    out << "  \"amat_formula\": " << r.amatFormula << "\n";
    out << "}\n";
}

}  // namespace cachesim
