// Minimal single-header unit test harness.
//
// No external dependencies: tests must run headless and offline in the
// container (pure CPU). If the project outgrows this, replace with a real
// framework (e.g. doctest/gtest) and keep the VP_TEST/VP_CHECK names.

#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace vpt {

struct Case {
  const char* name;
  std::function<void()> fn;
};

inline std::vector<Case>& cases() {
  static std::vector<Case> c;
  return c;
}

inline int& failureCount() {
  static int n = 0;
  return n;
}

inline const char*& currentCase() {
  static const char* n = "";
  return n;
}

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) {
    cases().push_back({name, std::move(fn)});
  }
};

inline void checkFail(const char* expr, const char* file, int line,
                      const std::string& detail = "") {
  ++failureCount();
  std::fprintf(stderr, "FAIL [%s] %s:%d: %s%s\n", currentCase(), file, line,
               expr, detail.empty() ? "" : ("  " + detail).c_str());
}

// Run all registered cases; returns the number of failed checks.
inline int runAll() {
  int before = failureCount();
  for (auto& c : cases()) {
    currentCase() = c.name;
    c.fn();
  }
  return failureCount() - before;
}

}  // namespace vpt

#define VP_TEST(name)                                    \
  static void name();                                    \
  static ::vpt::Registrar name##_registrar(#name, name); \
  static void name()

#define VP_CHECK(cond)                                        \
  do {                                                        \
    if (!(cond)) ::vpt::checkFail(#cond, __FILE__, __LINE__); \
  } while (0)

#define VP_CHECK_NEAR(a, b, tol)                                              \
  do {                                                                        \
    double va = (a), vb = (b), vt = (tol);                                    \
    if (std::fabs(va - vb) > vt) {                                            \
      char buf[160];                                                          \
      std::snprintf(buf, sizeof(buf), "got %.12g, want %.12g (+/- %.3g)", va, \
                    vb, vt);                                                  \
      ::vpt::checkFail(#a " ~= " #b, __FILE__, __LINE__, buf);                \
    }                                                                         \
  } while (0)
