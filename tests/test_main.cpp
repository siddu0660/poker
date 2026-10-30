#include "test_main.hpp"

#include <cstring>

namespace pftest {
std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
int& failureCount() { static int n = 0; return n; }
}  // namespace pftest

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0, failed = 0;
    for (const auto& t : pftest::registry()) {
        if (filter && t.name.find(filter) == std::string::npos) continue;
        const int before = pftest::failureCount();
        t.fn();
        ++ran;
        if (pftest::failureCount() != before) { ++failed; std::cerr << "FAIL " << t.name << "\n"; }
        else std::cout << "ok   " << t.name << "\n";
    }
    std::cout << ran - failed << "/" << ran << " tests passed\n";
    return failed == 0 ? 0 : 1;
}
