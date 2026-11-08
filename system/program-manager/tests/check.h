// Minimal host test helpers: counting checks, non-zero exit on the first failure summary.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace check {
inline int& Failures() { static int failures = 0; return failures; }
inline int& Count() { static int count = 0; return count; }
inline void Report(bool ok, const char* expr, const char* file, int line) {
    ++Count();
    if(!ok) {
        ++Failures();
        std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
    }
}
inline int Finish(const char* suite) {
    std::printf("%s: %d checks, %d failures\n", suite, Count(), Failures());
    return Failures() ? 1 : 0;
}
} // namespace check

#define CHECK(expr) check::Report((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b) check::Report((a) == (b), #a " == " #b, __FILE__, __LINE__)
#define CHECK_STREQ(a, b) check::Report(std::strcmp((a), (b)) == 0, #a " == " #b, __FILE__, __LINE__)
