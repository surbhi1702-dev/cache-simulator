#include "cachesim/hierarchy.hpp"
#include "cachesim/trace.hpp"
#include "test_framework.hpp"

using namespace cachesim;

namespace {
SimConfig twoLevel(const std::string& l1, const std::string& l2, Cycles mem = 100) {
    SimConfig c;
    c.levels = {parseLevelSpec(l1), parseLevelSpec(l2)};
    c.memoryLatency = mem;
    return c;
}
MemAccess rd(Addr a) { return {a, AccessType::Read, 4}; }
MemAccess wr(Addr a) { return {a, AccessType::Write, 4}; }
}  // namespace

TEST(Hierarchy, LatencyPerLevel) {
    // L1: 2 direct-mapped lines (0x0 and 0x80 collide), L2 large.
    Hierarchy h(twoLevel("name=L1,size=128,assoc=1,block=64,hit=4", "name=L2,size=4K,assoc=4,block=64,hit=12"));
    EXPECT_EQ(h.access(rd(0x000)), 4u + 12u + 100u);  // memory
    EXPECT_EQ(h.access(rd(0x080)), 116u);
    EXPECT_EQ(h.access(rd(0x000)), 16u);  // L1 miss, L2 hit
    EXPECT_EQ(h.access(rd(0x004)), 4u);   // L1 hit
}

TEST(Hierarchy, DirtyVictimGoesToL2NotMemory) {
    Hierarchy h(twoLevel("name=L1,size=128,assoc=1,block=64", "name=L2,size=4K,assoc=4,block=64"));
    h.access(wr(0x000));
    h.access(rd(0x080));  // evicts dirty 0x0 from L1
    EXPECT_EQ(h.level(0).stats().writebacks, 1u);
    EXPECT_EQ(h.level(1).stats().writesFromAbove, 1u);
    EXPECT_EQ(h.memory().stats().writes, 0u);
    EXPECT_EQ(h.level(1).dirtyLines(), 1u);
}

TEST(Hierarchy, MeasuredAmatMatchesFormulaForWriteBack) {
    SimConfig c = SimConfig::defaults();
    c.trace.pattern = TracePattern::Random;
    c.trace.numAccesses = 50000;
    c.trace.workingSetBytes = 4 * 1024 * 1024;
    Hierarchy h(c);
    h.run(generateTrace(c.trace));
    auto r = h.result();
    EXPECT_NEAR(r.amat, r.amatFormula, 1e-9);
    // With only write-back levels, every memory read is a last-level miss.
    EXPECT_EQ(r.memory.reads, r.levels.back().stats.misses());
    // Each lower level sees exactly the demand misses of the level above.
    EXPECT_EQ(r.levels[1].stats.accesses(), r.levels[0].stats.misses());
    EXPECT_EQ(r.levels[2].stats.accesses(), r.levels[1].stats.misses());
}

TEST(Hierarchy, WorkingSetThatFitsOnlyHasColdMisses) {
    SimConfig c = SimConfig::defaults();
    c.trace.pattern = TracePattern::Sequential;
    c.trace.workingSetBytes = 16 * 1024;  // half of L1
    c.trace.numAccesses = 40960;          // 10 passes
    Hierarchy h(c);
    h.run(generateTrace(c.trace));
    auto r = h.result();
    EXPECT_EQ(r.levels[0].stats.misses(), 256u);  // 16K / 64B
    EXPECT_EQ(r.levels[0].stats.compulsoryMisses, 256u);
}

TEST(Hierarchy, MoreWaysNeverHurtLru) {
    // LRU is a stack algorithm: with the set count fixed, adding ways can only
    // add hits (no Belady anomaly).
    TraceConfig t;
    t.pattern = TracePattern::Random;
    t.numAccesses = 30000;
    t.workingSetBytes = 64 * 1024;
    auto trace = generateTrace(t);
    double prev = -1.0;
    for (int ways : {1, 2, 4, 8, 16}) {
        SimConfig c;
        c.levels = {parseLevelSpec("size=" + std::to_string(64 * 64 * ways) + ",block=64,assoc=" + std::to_string(ways))};
        Hierarchy h(c);
        h.run(trace);
        double hr = h.result().levels[0].stats.hitRate();
        EXPECT_TRUE(hr >= prev);
        prev = hr;
    }
}

TEST(Hierarchy, WriteThroughGeneratesMoreMemoryTraffic) {
    TraceConfig t;
    t.pattern = TracePattern::Sequential;
    t.numAccesses = 20000;
    t.workingSetBytes = 8 * 1024;
    t.writeRatio = 0.5;
    auto trace = generateTrace(t);
    auto bytesWritten = [&](const std::string& wp) {
        SimConfig c;
        c.levels = {parseLevelSpec("size=16K,assoc=4,block=64,write=" + wp)};
        Hierarchy h(c);
        h.run(trace);
        return h.result().memory.bytesWritten;
    };
    EXPECT_EQ(bytesWritten("wb"), 0u);  // all dirty lines still resident
    EXPECT_TRUE(bytesWritten("wt") > 30000u);
}
