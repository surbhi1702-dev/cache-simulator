#include "cachesim/replacement_policy.hpp"
#include "helpers.hpp"
#include "test_framework.hpp"

using namespace cachesim;

// One set, two ways, 64-byte blocks. Blocks A, B, C all map to the only set.
// Sequence A B A C A separates the policies:
//   LRU : C evicts B (A was just used)      -> final A hits   -> 2 hits
//   FIFO: C evicts A (A was inserted first) -> final A misses -> 1 hit
namespace {
constexpr Addr A = 0x000, B = 0x040, C = 0x080;

std::uint64_t hitsFor(const std::string& policy) {
    SingleCache s("size=128,block=64,assoc=2,policy=" + policy);
    for (Addr a : {A, B, A, C, A}) s.read(a);
    return s.cache.stats().hits();
}
}  // namespace

TEST(Replacement, LruKeepsRecentlyUsed) {
    EXPECT_EQ(hitsFor("lru"), 2u);
    SingleCache s("size=128,block=64,assoc=2,policy=lru");
    for (Addr a : {A, B, A, C}) s.read(a);
    EXPECT_TRUE(s.cache.contains(A));
    EXPECT_FALSE(s.cache.contains(B));
    EXPECT_TRUE(s.cache.contains(C));
}

TEST(Replacement, FifoIgnoresHits) {
    EXPECT_EQ(hitsFor("fifo"), 1u);
    SingleCache s("size=128,block=64,assoc=2,policy=fifo");
    for (Addr a : {A, B, A, C}) s.read(a);
    EXPECT_FALSE(s.cache.contains(A));
    EXPECT_TRUE(s.cache.contains(B));
}

TEST(Replacement, LruPolicyUnit) {
    LruPolicy p(1, 4);
    for (std::size_t w = 0; w < 4; ++w) p.onFill(0, w);
    EXPECT_EQ(p.victim(0), 0u);
    p.onHit(0, 0);
    EXPECT_EQ(p.victim(0), 1u);
    p.onHit(0, 1);
    p.onHit(0, 2);
    EXPECT_EQ(p.victim(0), 3u);
}

TEST(Replacement, FifoPolicyUnit) {
    FifoPolicy p(2, 2);
    p.onFill(1, 1);
    p.onFill(1, 0);
    p.onHit(1, 1);
    EXPECT_EQ(p.victim(1), 1u);  // inserted first, hit does not matter
}

TEST(Replacement, RandomIsSeededAndInRange) {
    RandomPolicy a(8, 7), b(8, 7);
    bool seen[8] = {};
    for (int i = 0; i < 1000; ++i) {
        auto va = a.victim(0);
        EXPECT_EQ(va, b.victim(0));
        ASSERT_TRUE(va < 8);
        seen[va] = true;
    }
    for (bool s : seen) EXPECT_TRUE(s);
}

TEST(Replacement, LoopLargerThanCacheThrashesLru) {
    // Cyclic access to ways+1 blocks: LRU always evicts the next block needed
    // (0% hits after warm-up) while Random keeps some of them.
    auto run = [](const std::string& policy) {
        SingleCache s("size=256,block=64,assoc=4,policy=" + policy);
        for (int pass = 0; pass < 200; ++pass)
            for (Addr blk = 0; blk < 5; ++blk) s.read(blk * 64);
        return s.cache.stats().hits();
    };
    EXPECT_EQ(run("lru"), 0u);
    EXPECT_EQ(run("fifo"), 0u);
    EXPECT_TRUE(run("random") > 100u);
}
