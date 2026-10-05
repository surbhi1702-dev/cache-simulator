#include "cachesim/config.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace cachesim {

namespace {

bool isPowerOfTwo(std::uint64_t x) { return x != 0 && (x & (x - 1)) == 0; }

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::uint64_t parseUnsigned(const std::string& text, const std::string& what) {
    try {
        std::size_t used = 0;
        auto v = std::stoull(text, &used, 0);
        if (used != text.size()) throw std::invalid_argument("trailing");
        return v;
    } catch (const std::exception&) {
        throw ConfigError("invalid " + what + ": '" + text + "'");
    }
}

}  // namespace

void CacheConfig::validate() const {
    const std::string where = "cache " + name + ": ";
    if (!isPowerOfTwo(blockBytes)) throw ConfigError(where + "block size must be a power of two");
    if (sizeBytes < blockBytes || sizeBytes % blockBytes != 0)
        throw ConfigError(where + "size must be a multiple of the block size");
    if (associativity == 0 || numLines() % associativity != 0)
        throw ConfigError(where + "associativity must divide the number of lines (" +
                          std::to_string(numLines()) + ")");
    if (!isPowerOfTwo(numSets()))
        throw ConfigError(where + "number of sets (" + std::to_string(numSets()) +
                          ") must be a power of two");
}

std::string CacheConfig::organisation() const {
    if (associativity == 1) return "direct-mapped";
    if (associativity == numLines()) return "fully-assoc";
    return std::to_string(associativity) + "-way";
}

void SimConfig::validate() const {
    if (levels.empty()) throw ConfigError("at least one cache level is required");
    for (const auto& l : levels) l.validate();
    for (std::size_t i = 1; i < levels.size(); ++i) {
        if (levels[i].blockBytes < levels[i - 1].blockBytes)
            throw ConfigError("cache " + levels[i].name +
                              ": block size must be >= the block size of the level above");
    }
    if (trace.pattern != TracePattern::File) {
        if (trace.numAccesses == 0) throw ConfigError("number of accesses must be > 0");
        if (trace.elementBytes == 0 || trace.workingSetBytes < trace.elementBytes)
            throw ConfigError("working set must be at least one element");
        if (trace.pattern == TracePattern::Strided && trace.strideBytes == 0)
            throw ConfigError("stride must be > 0");
    } else if (trace.tracePath.empty()) {
        throw ConfigError("pattern 'file' needs --trace <path>");
    }
    if (trace.writeRatio < 0.0 || trace.writeRatio > 1.0)
        throw ConfigError("write ratio must be within [0, 1]");
}

SimConfig SimConfig::defaults() {
    SimConfig c;
    c.levels.push_back(parseLevelSpec("name=L1,size=32K,assoc=8,block=64,policy=lru,write=wb,hit=4"));
    c.levels.push_back(parseLevelSpec("name=L2,size=256K,assoc=8,block=64,policy=lru,write=wb,hit=12"));
    c.levels.push_back(parseLevelSpec("name=L3,size=2M,assoc=16,block=64,policy=lru,write=wb,hit=40"));
    c.memoryLatency = 200;
    return c;
}

std::uint64_t parseSize(const std::string& raw) {
    std::string text = lower(trim(raw));
    if (text.empty()) throw ConfigError("empty size");
    std::uint64_t mult = 1;
    if (text.size() > 1 && text.back() == 'b' && !std::isdigit(static_cast<unsigned char>(text[text.size() - 2])))
        text.pop_back();  // "32kb" -> "32k"
    switch (text.back()) {
        case 'k': mult = 1ULL << 10; text.pop_back(); break;
        case 'm': mult = 1ULL << 20; text.pop_back(); break;
        case 'g': mult = 1ULL << 30; text.pop_back(); break;
        default: break;
    }
    return parseUnsigned(text, "size '" + raw + "'") * mult;
}

ReplacementKind parseReplacement(const std::string& text) {
    auto t = lower(trim(text));
    if (t == "lru") return ReplacementKind::LRU;
    if (t == "fifo") return ReplacementKind::FIFO;
    if (t == "random" || t == "rand" || t == "rr") return ReplacementKind::Random;
    throw ConfigError("unknown replacement policy '" + text + "' (lru|fifo|random)");
}

WritePolicy parseWritePolicy(const std::string& text) {
    auto t = lower(trim(text));
    if (t == "wb" || t == "write-back" || t == "writeback") return WritePolicy::WriteBack;
    if (t == "wt" || t == "write-through" || t == "writethrough") return WritePolicy::WriteThrough;
    throw ConfigError("unknown write policy '" + text + "' (wb|wt)");
}

TracePattern parsePattern(const std::string& text) {
    auto t = lower(trim(text));
    if (t == "seq" || t == "sequential") return TracePattern::Sequential;
    if (t == "random" || t == "rand") return TracePattern::Random;
    if (t == "stride" || t == "strided") return TracePattern::Strided;
    if (t == "file") return TracePattern::File;
    throw ConfigError("unknown pattern '" + text + "' (sequential|random|strided|file)");
}

std::string toString(TracePattern pattern) {
    switch (pattern) {
        case TracePattern::Sequential: return "sequential";
        case TracePattern::Random: return "random";
        case TracePattern::Strided: return "strided";
        case TracePattern::File: return "file";
    }
    return "?";
}

CacheConfig parseLevelSpec(const std::string& spec) {
    CacheConfig c;
    bool fullyAssoc = false;
    std::stringstream ss(spec);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (item.empty()) continue;
        auto eq = item.find('=');
        if (eq == std::string::npos) throw ConfigError("expected key=value in level spec, got '" + item + "'");
        auto key = lower(trim(item.substr(0, eq)));
        auto value = trim(item.substr(eq + 1));
        if (key == "name") c.name = value;
        else if (key == "size") c.sizeBytes = parseSize(value);
        else if (key == "assoc" || key == "ways") {
            if (lower(value) == "full" || lower(value) == "fa") fullyAssoc = true;
            else c.associativity = static_cast<std::uint32_t>(parseUnsigned(value, "associativity"));
        } else if (key == "block" || key == "line") c.blockBytes = static_cast<std::uint32_t>(parseSize(value));
        else if (key == "policy" || key == "repl") c.replacement = parseReplacement(value);
        else if (key == "write") c.writePolicy = parseWritePolicy(value);
        else if (key == "hit" || key == "latency") c.hitLatency = parseUnsigned(value, "hit latency");
        else throw ConfigError("unknown level key '" + key + "'");
    }
    if (fullyAssoc) c.associativity = static_cast<std::uint32_t>(c.numLines());
    return c;
}

}  // namespace cachesim
