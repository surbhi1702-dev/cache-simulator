#include <cstdio>
#include <set>

#include "cachesim/trace.hpp"
#include "test_framework.hpp"

using namespace cachesim;

TEST(Trace, SequentialStepsAndWraps) {
    TraceConfig t;
    t.pattern = TracePattern::Sequential;
    t.baseAddr = 0x1000;
    t.workingSetBytes = 64;
    t.elementBytes = 8;
    t.numAccesses = 10;
    auto tr = generateTrace(t);
    ASSERT_EQ(tr.size(), 10u);
    EXPECT_EQ(tr[0].addr, 0x1000u);
    EXPECT_EQ(tr[1].addr, 0x1008u);
    EXPECT_EQ(tr[7].addr, 0x1038u);
    EXPECT_EQ(tr[8].addr, 0x1000u);  // wrapped
    EXPECT_EQ(tr[0].size, 8u);
}

TEST(Trace, StridedJumpsByStride) {
    TraceConfig t;
    t.pattern = TracePattern::Strided;
    t.baseAddr = 0;
    t.workingSetBytes = 1024;
    t.strideBytes = 256;
    t.elementBytes = 4;
    t.numAccesses = 8;
    auto tr = generateTrace(t);
    EXPECT_EQ(tr[0].addr, 0u);
    EXPECT_EQ(tr[1].addr, 256u);
    EXPECT_EQ(tr[3].addr, 768u);
    EXPECT_EQ(tr[4].addr, 4u);  // second pass shifted by one element
    EXPECT_EQ(tr[5].addr, 260u);
}

TEST(Trace, RandomIsAlignedBoundedAndSeeded) {
    TraceConfig t;
    t.pattern = TracePattern::Random;
    t.baseAddr = 0x4000;
    t.workingSetBytes = 4096;
    t.numAccesses = 5000;
    auto a = generateTrace(t), b = generateTrace(t);
    std::set<Addr> distinct;
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].addr, b[i].addr);
        ASSERT_TRUE(a[i].addr >= 0x4000 && a[i].addr < 0x5000);
        EXPECT_EQ(a[i].addr % 4, 0u);
        distinct.insert(a[i].addr);
    }
    EXPECT_TRUE(distinct.size() > 900u);  // ~1024 possible
    t.seed = 99;
    EXPECT_FALSE(generateTrace(t)[0].addr == a[0].addr && generateTrace(t)[1].addr == a[1].addr);
}

TEST(Trace, WriteRatioIsRespected) {
    TraceConfig t;
    t.numAccesses = 100000;
    t.writeRatio = 0.25;
    std::size_t writes = 0;
    for (const auto& a : generateTrace(t)) writes += a.type == AccessType::Write;
    EXPECT_NEAR(double(writes) / 100000.0, 0.25, 0.01);
}

TEST(Trace, SaveLoadRoundTrip) {
    TraceConfig t;
    t.pattern = TracePattern::Random;
    t.numAccesses = 100;
    auto tr = generateTrace(t);
    const std::string path = "cachesim_test_roundtrip.trace";
    saveTrace(path, tr);
    auto back = loadTrace(path);
    std::remove(path.c_str());
    ASSERT_EQ(back.size(), tr.size());
    for (std::size_t i = 0; i < tr.size(); ++i) {
        EXPECT_EQ(back[i].addr, tr[i].addr);
        EXPECT_TRUE(back[i].type == tr[i].type);
    }
}

TEST(Trace, LoadsBundledTraceFile) {
    auto tr = loadTrace(std::string(CACHESIM_TEST_DATA_DIR) + "/matrix_8x8.trace");
    EXPECT_EQ(tr.size(), 128u);
    EXPECT_TRUE(tr[0].type == AccessType::Read);
    EXPECT_THROW(loadTrace("does/not/exist.trace"), ConfigError);
}
