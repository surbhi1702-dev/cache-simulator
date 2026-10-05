// cachesim: command-line front end.
//
//   cachesim --level name=L1,size=32K,assoc=8,block=64,policy=lru,write=wb,hit=4 \
//            --level name=L2,size=256K,assoc=8,hit=12 --mem-latency 100 \
//            --pattern random --accesses 200000 --working-set 1M --format json
//
// Options can also come from a config file (--config file.cfg) holding one
// "option = value" per line, e.g. "level = name=L1,size=32K".
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>

#include "cachesim/hierarchy.hpp"
#include "cachesim/report.hpp"
#include "cachesim/trace.hpp"

using namespace cachesim;

namespace {

constexpr const char* kUsage = R"(Usage: cachesim [options]

Hierarchy
  --level SPEC          add a cache level (first = L1). SPEC is comma-separated
                        key=value: name, size (32K, 1M), assoc (N or 'full'),
                        block, policy (lru|fifo|random), write (wb|wt), hit
  --mem-latency N       main-memory latency in cycles            [200]
  --seed N              seed for trace generation and Random     [42]
                        replacement
Trace
  --pattern P           sequential | random | strided | file     [sequential]
  --trace PATH          trace file for --pattern file (R/W addr [size])
  --accesses N          number of generated accesses             [100000]
  --working-set SIZE    region the generated addresses wrap in   [256K]
  --element SIZE        bytes per access                         [4]
  --stride SIZE         stride for --pattern strided             [64]
  --write-ratio F       fraction of accesses that are stores     [0.3]
  --base ADDR           base address of the region               [0x10000000]
  --dump-trace PATH     also write the generated trace to PATH
Output
  --format F            text | csv | json                        [text]
  --output PATH         write results to PATH instead of stdout
  --no-header           omit the CSV header (for appending sweeps)
  --label TEXT          free-form label stored in the results
  --config PATH         read options from a file ("option = value" per line)
  -h, --help            show this message

If no --level is given, a 3-level default hierarchy is used:
  L1 32K 8-way 4cy, L2 256K 8-way 12cy, L3 2M 16-way 40cy, memory 200cy.
)";

struct Options {
    SimConfig sim;
    bool levelsGiven = false;
    std::string format = "text";
    std::string output;
    std::string dumpTrace;
    bool header = true;
};

void applyOption(Options& o, const std::string& key, const std::string& value);

void loadConfigFile(Options& o, const std::string& path) {
    std::ifstream in(path);
    if (!in) throw ConfigError("cannot open config file '" + path + "'");
    std::string line;
    while (std::getline(in, line)) {
        if (auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        auto eq = line.find('=');
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        if (eq == std::string::npos) throw ConfigError("config line without '=': " + line);
        auto trim = [](std::string s) {
            auto b = s.find_first_not_of(" \t\r");
            auto e = s.find_last_not_of(" \t\r");
            return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
        };
        applyOption(o, "--" + trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
    }
}

void applyOption(Options& o, const std::string& key, const std::string& value) {
    static const std::map<std::string, std::function<void(Options&, const std::string&)>> handlers = {
        {"--level", [](Options& op, const std::string& v) {
             if (!op.levelsGiven) op.sim.levels.clear();
             op.levelsGiven = true;
             auto c = parseLevelSpec(v);
             if (c.name == "L1" && !op.sim.levels.empty()) c.name = "L" + std::to_string(op.sim.levels.size() + 1);
             op.sim.levels.push_back(c);
         }},
        {"--mem-latency", [](Options& op, const std::string& v) { op.sim.memoryLatency = std::stoull(v); }},
        {"--seed", [](Options& op, const std::string& v) { op.sim.seed = op.sim.trace.seed = std::stoull(v); }},
        {"--pattern", [](Options& op, const std::string& v) { op.sim.trace.pattern = parsePattern(v); }},
        {"--trace", [](Options& op, const std::string& v) {
             op.sim.trace.tracePath = v;
             op.sim.trace.pattern = TracePattern::File;
         }},
        {"--accesses", [](Options& op, const std::string& v) { op.sim.trace.numAccesses = parseSize(v); }},
        {"--working-set", [](Options& op, const std::string& v) { op.sim.trace.workingSetBytes = parseSize(v); }},
        {"--element", [](Options& op, const std::string& v) { op.sim.trace.elementBytes = static_cast<std::uint32_t>(parseSize(v)); }},
        {"--stride", [](Options& op, const std::string& v) { op.sim.trace.strideBytes = parseSize(v); }},
        {"--write-ratio", [](Options& op, const std::string& v) { op.sim.trace.writeRatio = std::stod(v); }},
        {"--base", [](Options& op, const std::string& v) { op.sim.trace.baseAddr = std::stoull(v, nullptr, 0); }},
        {"--dump-trace", [](Options& op, const std::string& v) { op.dumpTrace = v; }},
        {"--format", [](Options& op, const std::string& v) {
             if (v != "text" && v != "csv" && v != "json") throw ConfigError("--format must be text, csv or json");
             op.format = v;
         }},
        {"--output", [](Options& op, const std::string& v) { op.output = v; }},
        {"--label", [](Options& op, const std::string& v) { op.sim.label = v; }},
        {"--config", [](Options& op, const std::string& v) { loadConfigFile(op, v); }},
    };
    auto it = handlers.find(key);
    if (it == handlers.end()) throw ConfigError("unknown option '" + key + "' (see --help)");
    try {
        it->second(o, value);
    } catch (const ConfigError&) {
        throw;
    } catch (const std::exception&) {
        throw ConfigError("invalid value '" + value + "' for " + key);
    }
}

Options parseArgs(int argc, char** argv) {
    Options o;
    o.sim = SimConfig::defaults();
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << kUsage;
            std::exit(0);
        }
        if (arg == "--no-header") {
            o.header = false;
            continue;
        }
        std::string value;
        if (auto eq = arg.find('='); arg.rfind("--", 0) == 0 && eq != std::string::npos) {
            value = arg.substr(eq + 1);
            arg = arg.substr(0, eq);
        } else {
            if (i + 1 >= argc) throw ConfigError("option " + arg + " needs a value");
            value = argv[++i];
        }
        applyOption(o, arg, value);
    }
    return o;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Options opt = parseArgs(argc, argv);
        opt.sim.validate();

        const auto trace = generateTrace(opt.sim.trace);
        if (!opt.dumpTrace.empty()) saveTrace(opt.dumpTrace, trace);

        Hierarchy hierarchy(opt.sim);
        hierarchy.run(trace);
        const auto result = hierarchy.result();

        std::ofstream file;
        std::ostream* out = &std::cout;
        if (!opt.output.empty()) {
            file.open(opt.output);
            if (!file) throw ConfigError("cannot write '" + opt.output + "'");
            out = &file;
        }
        if (opt.format == "json") writeJson(*out, result);
        else if (opt.format == "csv") writeCsv(*out, result, opt.header);
        else writeText(*out, result);
        return 0;
    } catch (const ConfigError& e) {
        std::cerr << "cachesim: " << e.what() << '\n';
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "cachesim: internal error: " << e.what() << '\n';
        return 1;
    }
}
