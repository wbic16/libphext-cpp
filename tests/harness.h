// Tiny conformance-test harness for the libphext-cpp parity port (P0).
// No external dependencies: a failed check prints the test name, line, and
// (for CHECK_EQ) both values with control bytes escaped, and the process
// exit code is the number of failed tests (capped at 125).
#pragma once
#include <cstdio>
#include <string>
#include <string_view>

namespace harness {
inline int& check_count() { static int n = 0; return n; }
inline int& failure_count() { static int n = 0; return n; }
inline int& test_count() { static int n = 0; return n; }
inline int& failed_test_count() { static int n = 0; return n; }
inline const char*& current_test() { static const char* t = ""; return t; }

inline std::string escape(std::string_view s) {
   std::string out;
   char buf[8];
   for (char raw : s) {
      const unsigned char b = static_cast<unsigned char>(raw);
      if (b >= 0x20 && b < 0x7F && b != '"' && b != '\\') {
         out.push_back(raw);
      } else {
         std::snprintf(buf, sizeof(buf), "\\x%02x", b);
         out += buf;
      }
   }
   return out;
}

inline void report_failure(const char* expr, int line) {
   failure_count() += 1;
   std::printf("  FAIL [%s] line %d: %s\n", current_test(), line, expr);
}

inline void run(const char* name, void (*fn)()) {
   current_test() = name;
   const int before = failure_count();
   fn();
   test_count() += 1;
   if (failure_count() != before) failed_test_count() += 1;
   std::printf("%s %s\n", failure_count() == before ? "PASS" : "FAIL", name);
}

inline int summary(const char* suite) {
   std::printf("%s: %d tests, %d checks, %d failures\n", suite, test_count(), check_count(), failure_count());
   return failed_test_count() > 125 ? 125 : failed_test_count();
}
} // namespace harness

#define CHECK(cond)                                                                              \
   do {                                                                                          \
      harness::check_count() += 1;                                                               \
      if (!(cond)) harness::report_failure(#cond, __LINE__);                                     \
   } while (0)

#define CHECK_EQ_STR(actual, expected)                                                           \
   do {                                                                                          \
      harness::check_count() += 1;                                                               \
      const std::string a_ = (actual);                                                           \
      const std::string e_ = (expected);                                                         \
      if (a_ != e_) {                                                                            \
         harness::failure_count() += 1;                                                          \
         std::printf("  FAIL [%s] line %d:\n    actual:   \"%s\"\n    expected: \"%s\"\n",        \
                     harness::current_test(), __LINE__, harness::escape(a_).c_str(),           \
                     harness::escape(e_).c_str());                                               \
      }                                                                                          \
   } while (0)

#define RUN(fn) harness::run(#fn, fn)
