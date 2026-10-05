#include "test_framework.hpp"

int main() {
    int failedTests = 0;
    for (const auto& t : testfw::registry()) {
        const int before = testfw::failures();
        try {
            t.fn();
        } catch (const testfw::FatalFailure&) {
        } catch (const std::exception& e) {
            testfw::report(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what());
        }
        const bool ok = testfw::failures() == before;
        failedTests += ok ? 0 : 1;
        std::cout << (ok ? "[  OK  ] " : "[ FAIL ] ") << t.name << "\n";
    }
    std::cout << "\n" << testfw::registry().size() - failedTests << "/" << testfw::registry().size()
              << " tests passed\n";
    return failedTests == 0 ? 0 : 1;
}
