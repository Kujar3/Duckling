#pragma once

// Minimal dependency-free test framework for the core tests.
//
//   TEST_CASE ("voicing is ascending") { CHECK (x < y); CHECK_EQ (a, b); REQUIRE (ok); }
//
// CHECK* failures are recorded and the test continues; REQUIRE* failures abort the current test case.

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace dktest
{
struct TestCase
{
    const char* name;
    const char* file;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failureCount()
{
    static int n = 0;
    return n;
}

struct RequireFailed {};

struct Registrar
{
    Registrar (const char* name, const char* file, std::function<void()> fn) { registry().push_back ({ name, file, std::move (fn) }); }
};

inline void reportFailure (const char* file, int line, const std::string& message)
{
    ++failureCount();
    std::cout << "    FAIL " << file << ":" << line << "  " << message << "\n";
}

template <typename T>
std::string show (const T& v)
{
    std::ostringstream os;
    if constexpr (std::is_enum_v<T>)
        os << (int) v;
    else
        os << v;
    return os.str();
}

template <typename T>
std::string show (const std::vector<T>& v)
{
    std::ostringstream os;
    os << "{";
    for (size_t i = 0; i < v.size(); ++i)
        os << (i ? ", " : "") << show (v[i]);
    os << "}";
    return os.str();
}
} // namespace dktest

#define CB_CONCAT2(a, b) a##b
#define CB_CONCAT(a, b) CB_CONCAT2 (a, b)

#define TEST_CASE(name)                                                                                   \
    static void CB_CONCAT (cbtest_fn_, __LINE__)();                                                       \
    static dktest::Registrar CB_CONCAT (cbtest_reg_, __LINE__) (name, __FILE__, &CB_CONCAT (cbtest_fn_, __LINE__)); \
    static void CB_CONCAT (cbtest_fn_, __LINE__)()

#define CHECK(cond)                                                                    \
    do { if (! (cond)) dktest::reportFailure (__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)

#define CHECK_EQ(a, b)                                                                                         \
    do {                                                                                                       \
        const auto& cb_a_ = (a);                                                                               \
        const auto& cb_b_ = (b);                                                                               \
        if (! (cb_a_ == cb_b_))                                                                                \
            dktest::reportFailure (__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b ")  got " + dktest::show (cb_a_) \
                                                           + " vs " + dktest::show (cb_b_));                   \
    } while (0)

#define REQUIRE(cond)                                                                    \
    do { if (! (cond)) { dktest::reportFailure (__FILE__, __LINE__, "REQUIRE(" #cond ")"); throw dktest::RequireFailed{}; } } while (0)
