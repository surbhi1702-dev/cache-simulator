#include "cachesim/trace.hpp"

#include <fstream>
#include <random>
#include <sstream>

namespace cachesim {

std::vector<MemAccess> generateTrace(const TraceConfig& cfg) {
    if (cfg.pattern == TracePattern::File) return loadTrace(cfg.tracePath, cfg.elementBytes);

    std::vector<MemAccess> trace;
    trace.reserve(cfg.numAccesses);
    std::mt19937_64 rng(cfg.seed);
    std::bernoulli_distribution isWrite(cfg.writeRatio);

    const std::uint64_t elems = cfg.workingSetBytes / cfg.elementBytes;
    std::uniform_int_distribution<std::uint64_t> pick(0, elems - 1);

    for (std::uint64_t i = 0; i < cfg.numAccesses; ++i) {
        std::uint64_t offset = 0;
        switch (cfg.pattern) {
            case TracePattern::Sequential:
                // a[i] for i = 0.. ; wraps to model repeated passes over an array
                offset = (i % elems) * cfg.elementBytes;
                break;
            case TracePattern::Strided:
                // a[i * k]: one access every `stride` bytes, wrapping in the
                // working set; each pass is shifted by one element so every
                // element is eventually visited (column-major traversal).
                {
                    const std::uint64_t perPass = (cfg.workingSetBytes + cfg.strideBytes - 1) / cfg.strideBytes;
                    const std::uint64_t pass = i / perPass;
                    const std::uint64_t col = (pass * cfg.elementBytes) % cfg.strideBytes;
                    offset = ((i % perPass) * cfg.strideBytes + col) % cfg.workingSetBytes;
                    offset -= offset % cfg.elementBytes;
                }
                break;
            case TracePattern::Random:
                offset = pick(rng) * cfg.elementBytes;
                break;
            case TracePattern::File:
                break;
        }
        MemAccess a;
        a.addr = cfg.baseAddr + offset;
        a.type = isWrite(rng) ? AccessType::Write : AccessType::Read;
        a.size = cfg.elementBytes;
        trace.push_back(a);
    }
    return trace;
}

std::vector<MemAccess> loadTrace(const std::string& path, std::uint32_t defaultSize) {
    std::ifstream in(path);
    if (!in) throw ConfigError("cannot open trace file '" + path + "'");

    std::vector<MemAccess> trace;
    std::string line;
    std::size_t lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        std::istringstream ss(line);
        std::string op, addr, size;
        if (!(ss >> op)) continue;  // blank / comment-only
        if (!(ss >> addr))
            throw ConfigError(path + ":" + std::to_string(lineNo) + ": missing address");

        MemAccess a;
        if (op == "R" || op == "r" || op == "L" || op == "l" || op == "0") a.type = AccessType::Read;
        else if (op == "W" || op == "w" || op == "S" || op == "s" || op == "1") a.type = AccessType::Write;
        else throw ConfigError(path + ":" + std::to_string(lineNo) + ": unknown op '" + op + "'");
        try {
            // Hex if prefixed with 0x; bare hex digits (Dinero style) also accepted.
            const bool hex = addr.rfind("0x", 0) == 0 || addr.rfind("0X", 0) == 0 ||
                             addr.find_first_of("abcdefABCDEF") != std::string::npos;
            a.addr = std::stoull(addr, nullptr, hex ? 16 : 10);
            a.size = (ss >> size) ? static_cast<std::uint32_t>(std::stoul(size)) : defaultSize;
        } catch (const std::exception&) {
            throw ConfigError(path + ":" + std::to_string(lineNo) + ": bad address/size");
        }
        trace.push_back(a);
    }
    return trace;
}

void saveTrace(const std::string& path, const std::vector<MemAccess>& trace) {
    std::ofstream out(path);
    if (!out) throw ConfigError("cannot write trace file '" + path + "'");
    out << "# op address size\n";
    for (const auto& a : trace) {
        out << toString(a.type) << " 0x" << std::hex << a.addr << std::dec << ' ' << a.size << '\n';
    }
}

}  // namespace cachesim
