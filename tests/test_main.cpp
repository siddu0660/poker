#include "test_main.hpp"

#include <cstring>
#include <exception>

namespace pftest {
std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
int& failureCount() { static int n = 0; return n; }
}  // namespace pftest

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0, failed = 0;
    for (const auto& t : pftest::registry()) {
        if (filter && t.name.find(filter) == std::string::npos) continue;
        // Name the test before it runs and flush: if the process dies hard, the log still says where.
        std::cout << "run  " << t.name << std::endl;
        const int before = pftest::failureCount();
        try {
            t.fn();
        } catch (const std::exception& e) {
            ++pftest::failureCount();
            std::cerr << "  uncaught exception: " << e.what() << std::endl;
        } catch (...) {
            ++pftest::failureCount();
            std::cerr << "  uncaught non-standard exception" << std::endl;
        }
        ++ran;
        if (pftest::failureCount() != before) { ++failed; std::cerr << "FAIL " << t.name << std::endl; }
        else std::cout << "ok   " << t.name << std::endl;
    }
    std::cout << ran - failed << "/" << ran << " tests passed" << std::endl;
    return failed == 0 ? 0 : 1;
}
