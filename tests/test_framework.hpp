// Minimal self-contained unit-test framework (no external dependencies, so the
// project builds offline). Usage:
//     TEST(SuiteName, CaseName) { EXPECT_EQ(a, b); ASSERT_TRUE(x); }
#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace testfw {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failures() {
    static int n = 0;
    return n;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

struct FatalFailure {};

inline void report(const char* file, int line, const std::string& msg) {
    ++failures();
    std::cerr << "    " << file << ":" << line << ": " << msg << "\n";
}

}  // namespace testfw

#define TEST(suite, name)                                                                   \
    static void suite##_##name();                                                           \
    static testfw::Registrar suite##_##name##_reg(#suite "." #name, suite##_##name);         \
    static void suite##_##name()

#define CHECK_IMPL(cond, msg, fatal)                                                        \
    do {                                                                                    \
        if (!(cond)) {                                                                      \
            std::ostringstream os_;                                                         \
            os_ << msg;                                                                     \
            testfw::report(__FILE__, __LINE__, os_.str());                                  \
            if (fatal) throw testfw::FatalFailure{};                                        \
        }                                                                                   \
    } while (0)

#define EXPECT_TRUE(c) CHECK_IMPL((c), "expected true: " #c, false)
#define EXPECT_FALSE(c) CHECK_IMPL(!(c), "expected false: " #c, false)
#define ASSERT_TRUE(c) CHECK_IMPL((c), "assertion failed: " #c, true)
#define EXPECT_EQ(a, b) \
    CHECK_IMPL((a) == (b), #a " == " #b " (got " << (a) << " vs " << (b) << ")", false)
#define ASSERT_EQ(a, b) \
    CHECK_IMPL((a) == (b), #a " == " #b " (got " << (a) << " vs " << (b) << ")", true)
#define EXPECT_NEAR(a, b, eps) \
    CHECK_IMPL(std::fabs(double(a) - double(b)) <= (eps), #a " ~= " #b " (got " << (a) << " vs " << (b) << ")", false)
#define EXPECT_THROW(stmt, ex)                                                              \
    do {                                                                                    \
        bool thrown_ = false;                                                               \
        try { stmt; } catch (const ex&) { thrown_ = true; }                                 \
        CHECK_IMPL(thrown_, "expected " #ex " from: " #stmt, false);                        \
    } while (0)
