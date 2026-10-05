#include "helpers.hpp"
#include "test_framework.hpp"

using namespace cachesim;

TEST(Cache, AddressDecomposition) {
    SingleCache s("size=32K,assoc=8,block=64");  // 64 sets
    EXPECT_EQ(s.cache.offsetBits(), 6u);
    EXPECT_EQ(s.cache.indexBits(), 6u);
    auto p = s.cache.decompose(0x12345);
    EXPECT_EQ(p.offset, 0x05u);
    EXPECT_EQ(p.index, 0x0Du);
    EXPECT_EQ(p.tag, 0x12u);
    EXPECT_EQ(s.cache.blockAddress(0x12345), 0x12340u);
}

TEST(Cache, ColdSequentialScanHitsWithinBlock) {
    SingleCache s("size=4K,assoc=1,block=64");
    for (Addr a = 0; a < 2048; a += 4) s.read(a);
    EXPECT_EQ(s.cache.stats().accesses(), 512u);
    EXPECT_EQ(s.cache.stats().misses(), 32u);  // one per 64-byte block
    EXPECT_NEAR(s.cache.stats().hitRate(), 15.0 / 16.0, 1e-12);
    EXPECT_EQ(s.cache.stats().compulsoryMisses, 32u);
}

TEST(Cache, DirectMappedConflictMisses) {
    // 1 KB direct-mapped, 64 B blocks -> 16 sets; 0x0 and 0x400 share set 0.
    SingleCache s("size=1K,assoc=1,block=64");
    for (int i = 0; i < 4; ++i) {
        s.read(0x000);
        s.read(0x400);
    }
    const auto& st = s.cache.stats();
    EXPECT_EQ(st.hits(), 0u);
    EXPECT_EQ(st.compulsoryMisses, 2u);
    EXPECT_EQ(st.conflictMisses, 6u);  // a fully-associative cache would hit
    EXPECT_EQ(st.capacityMisses, 0u);
}

TEST(Cache, AssociativityRemovesConflict) {
    SingleCache s("size=1K,assoc=2,block=64");
    for (int i = 0; i < 4; ++i) {
        s.read(0x000);
        s.read(0x400);
    }
    EXPECT_EQ(s.cache.stats().misses(), 2u);
    EXPECT_EQ(s.cache.stats().hits(), 6u);
}

TEST(Cache, CapacityMissesInFullyAssociative) {
    SingleCache s("size=256,assoc=full,block=64,policy=lru");  // 4 lines
    for (int pass = 0; pass < 10; ++pass)
        for (Addr b = 0; b < 5; ++b) s.read(b * 64);
    const auto& st = s.cache.stats();
    EXPECT_EQ(st.compulsoryMisses, 5u);
    EXPECT_EQ(st.capacityMisses, 45u);
    EXPECT_EQ(st.conflictMisses, 0u);
}

TEST(Cache, MissLatencyIncludesNextLevel) {
    SingleCache s("size=1K,assoc=1,block=64,hit=4", 100);
    EXPECT_EQ(s.read(0x40), 104u);
    EXPECT_EQ(s.read(0x44), 4u);
}

TEST(Cache, WriteBackDefersMemoryWrites) {
    SingleCache s("size=1K,assoc=1,block=64,write=wb");
    for (int i = 0; i < 10; ++i) s.write(0x000);  // 1 miss + 9 hits, all dirty in cache
    EXPECT_EQ(s.memory.stats().writes, 0u);
    EXPECT_EQ(s.cache.dirtyLines(), 1u);
    s.read(0x400);  // conflict evicts the dirty line
    EXPECT_EQ(s.cache.stats().writebacks, 1u);
    EXPECT_EQ(s.memory.stats().writes, 1u);
    EXPECT_EQ(s.memory.stats().bytesWritten, 64u);
    EXPECT_EQ(s.cache.dirtyLines(), 0u);
}

TEST(Cache, CleanEvictionDoesNotWriteBack) {
    SingleCache s("size=1K,assoc=1,block=64,write=wb");
    s.read(0x000);
    s.read(0x400);
    EXPECT_EQ(s.cache.stats().evictions, 1u);
    EXPECT_EQ(s.cache.stats().writebacks, 0u);
    EXPECT_EQ(s.memory.stats().writes, 0u);
}

TEST(Cache, WriteThroughSendsEveryStore) {
    SingleCache s("size=1K,assoc=1,block=64,write=wt");
    s.read(0x000);
    for (int i = 0; i < 10; ++i) s.write(0x000);
    EXPECT_EQ(s.cache.stats().writeHits, 10u);
    EXPECT_EQ(s.memory.stats().writes, 10u);
    EXPECT_EQ(s.memory.stats().bytesWritten, 40u);
    EXPECT_EQ(s.cache.dirtyLines(), 0u);
    s.read(0x400);
    EXPECT_EQ(s.cache.stats().writebacks, 0u);
}

TEST(Cache, WriteThroughIsNoWriteAllocate) {
    SingleCache s("size=1K,assoc=1,block=64,write=wt,hit=2", 100);
    EXPECT_EQ(s.write(0x80), 2u);  // buffered store, no fill
    EXPECT_FALSE(s.cache.contains(0x80));
    EXPECT_EQ(s.memory.stats().reads, 0u);
    s.read(0x80);
    EXPECT_EQ(s.cache.stats().readMisses, 1u);
}

TEST(Cache, WriteBackIsWriteAllocate) {
    SingleCache s("size=1K,assoc=1,block=64,write=wb");
    s.write(0x80);
    EXPECT_TRUE(s.cache.contains(0x80));
    EXPECT_EQ(s.memory.stats().reads, 1u);  // block fetched before the write
    s.read(0x84);
    EXPECT_EQ(s.cache.stats().readHits, 1u);
}
