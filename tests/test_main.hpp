#pragma once
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace pftest {

struct TestCase { std::string name; std::function<void()> fn; };
std::vector<TestCase>& registry();
int& failureCount();

struct Registrar {
    Registrar(std::string name, std::function<void()> fn) {
        registry().push_back({std::move(name), std::move(fn)});
    }
};

}  // namespace pftest

#define PF_TEST(name)                                                   \
    static void name();                                                 \
    static pftest::Registrar registrar_##name(#name, name);             \
    static void name()

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            ++pftest::failureCount();                                                \
            std::cerr << "  CHECK failed: " #cond " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
        }                                                                            \
    } while (0)

#define CHECK_EQ(a, b)                                                               \
    do {                                                                             \
        const auto va_ = (a);                                                        \
        const auto vb_ = (b);                                                        \
        if (!(va_ == vb_)) {                                                         \
            ++pftest::failureCount();                                                \
            std::cerr << "  CHECK_EQ failed: " #a " == " #b " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
        }                                                                            \
    } while (0)
