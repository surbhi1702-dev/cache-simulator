#include "cachesim/config.hpp"
#include "test_framework.hpp"

using namespace cachesim;

TEST(Config, ParseSizeUnits) {
    EXPECT_EQ(parseSize("64"), 64u);
    EXPECT_EQ(parseSize("32K"), 32u * 1024);
    EXPECT_EQ(parseSize("32kb"), 32u * 1024);
    EXPECT_EQ(parseSize("2M"), 2u * 1024 * 1024);
    EXPECT_EQ(parseSize("1G"), 1ull << 30);
    EXPECT_THROW(parseSize("abc"), ConfigError);
    EXPECT_THROW(parseSize(""), ConfigError);
}

TEST(Config, ParseLevelSpec) {
    auto c = parseLevelSpec("name=L2,size=256K,assoc=4,block=128,policy=fifo,write=wt,hit=10");
    EXPECT_EQ(c.name, "L2");
    EXPECT_EQ(c.sizeBytes, 256u * 1024);
    EXPECT_EQ(c.associativity, 4u);
    EXPECT_EQ(c.blockBytes, 128u);
    EXPECT_TRUE(c.replacement == ReplacementKind::FIFO);
    EXPECT_TRUE(c.writePolicy == WritePolicy::WriteThrough);
    EXPECT_EQ(c.hitLatency, 10u);
    EXPECT_EQ(c.numSets(), 512u);
    EXPECT_EQ(c.organisation(), "4-way");
}

TEST(Config, FullyAssociativeAndDirectMapped) {
    auto fa = parseLevelSpec("size=4K,block=64,assoc=full");
    EXPECT_EQ(fa.associativity, 64u);
    EXPECT_EQ(fa.numSets(), 1u);
    EXPECT_EQ(fa.organisation(), "fully-assoc");
    auto dm = parseLevelSpec("size=4K,block=64,assoc=1");
    EXPECT_EQ(dm.numSets(), 64u);
    EXPECT_EQ(dm.organisation(), "direct-mapped");
}

TEST(Config, RejectsUnrealisableGeometry) {
    EXPECT_THROW(parseLevelSpec("size=3000,block=64").validate(), ConfigError);   // not a multiple
    EXPECT_THROW(parseLevelSpec("size=4K,block=48").validate(), ConfigError);     // block not 2^n
    EXPECT_THROW(parseLevelSpec("size=12K,block=64,assoc=1").validate(), ConfigError);  // 192 sets
    EXPECT_THROW(parseLevelSpec("size=4K,block=64,assoc=3").validate(), ConfigError);
    EXPECT_THROW(parseLevelSpec("size=4K,colour=red"), ConfigError);
    EXPECT_THROW(parseLevelSpec("size=4K,policy=mru"), ConfigError);
}

TEST(Config, HierarchyRules) {
    SimConfig s = SimConfig::defaults();
    s.validate();
    s.levels[1].blockBytes = 32;  // L2 block smaller than L1 block
    EXPECT_THROW(s.validate(), ConfigError);
    SimConfig empty;
    EXPECT_THROW(empty.validate(), ConfigError);
}
