#pragma once

// A deliberately tiny test harness. The portable core has few tests and no
// need for fixtures, parameterisation or mocking, so a framework dependency
// would cost more than it saves.

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace mdu::test {

struct TestCase {
    const char* name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failureCount()
{
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

inline void reportFailure(const char* file, int line, const std::string& expression)
{
    ++failureCount();
    std::cerr << file << ":" << line << ": check failed: " << expression << "\n";
}

}  // namespace mdu::test

#define MDU_CONCAT_INNER(a, b) a##b
#define MDU_CONCAT(a, b) MDU_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                                  \
    static void MDU_CONCAT(testBody_, __LINE__)();                                       \
    static const ::mdu::test::Registrar MDU_CONCAT(testRegistrar_, __LINE__)(name,       \
                                                                             &MDU_CONCAT(testBody_, __LINE__)); \
    static void MDU_CONCAT(testBody_, __LINE__)()

// Variadic so expressions containing braced initialisers with commas work.
#define CHECK(...)                                                            \
    do {                                                                      \
        if (!(__VA_ARGS__)) {                                                 \
            ::mdu::test::reportFailure(__FILE__, __LINE__, #__VA_ARGS__);     \
        }                                                                     \
    } while (false)

#define CHECK_EQ(actual, expected)                                                                  \
    do {                                                                                            \
        const auto& actualValue_ = (actual);                                                        \
        const auto& expectedValue_ = (expected);                                                    \
        if (!(actualValue_ == expectedValue_)) {                                                    \
            ::mdu::test::reportFailure(__FILE__, __LINE__, #actual " == " #expected);               \
        }                                                                                           \
    } while (false)
