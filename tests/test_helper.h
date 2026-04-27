#pragma once
// Minimal test harness for engine unit tests.
// No framework deps — just assert + reporting.

#include <cstdio>

namespace md::test {

inline int g_tests_run    = 0;
inline int g_tests_failed = 0;

#define MD_CHECK(expr) do { \
    ++md::test::g_tests_run; \
    if (!(expr)) { \
        ++md::test::g_tests_failed; \
        fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); \
    } \
} while(0)

inline int TestSummary() {
    fprintf(stdout, "Tests: %d/%d passed\n",
            g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed;
}

} // namespace md::test
