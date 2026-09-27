// Copyright (c) 2026 Abdurrahman Konuk (professionally known as Ufuk Deniz Konuk)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Minimal dependency-free test harness: TEST(name) registers a case, CHECK records a failure
// and continues, REQUIRE aborts the current case.

#include <cstdio>
#include <exception>
#include <format>
#include <string>
#include <vector>

namespace anaf::TESTING {

  struct TestCase {
    const char* name;
    void (*body)();
  };

  inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
  }

  inline int& failureCount() {
    static int failures = 0;
    return failures;
  }

  struct RequireFailure : std::exception {};

  inline void reportFailure(const char* file, const int line, const std::string& message) {
    ++failureCount();
    std::printf("    FAIL %s:%d: %s\n", file, line, message.c_str());
  }

  inline int runAll(const std::string& filter) {
    int failedCases = 0;
    int ran = 0;
    for (const auto& test : registry()) {
      if (!filter.empty() && std::string(test.name).find(filter) == std::string::npos) continue;
      ++ran;
      const int before = failureCount();
      std::printf("[ RUN  ] %s\n", test.name);
      try {
        test.body();
      } catch (const RequireFailure&) {
      } catch (const std::exception& error) {
        reportFailure(__FILE__, __LINE__, std::string("unexpected exception: ") + error.what());
      }
      const bool passed = failureCount() == before;
      failedCases += passed ? 0 : 1;
      std::printf("[ %s ] %s\n", passed ? " OK " : "FAIL", test.name);
    }
    std::printf("\n%d test cases, %d failed, %d failed checks\n", ran, failedCases, failureCount());
    return failedCases == 0 ? 0 : 1;
  }

} // namespace anaf::TESTING end

#define ANAF_CONCAT_INNER(a, b) a##b
#define ANAF_CONCAT(a, b) ANAF_CONCAT_INNER(a, b)

#define TEST(name)                                                                              \
  static void name();                                                                           \
  static const bool ANAF_CONCAT(name, _registered) =                                            \
    (anaf::TESTING::registry().push_back({#name, &name}), true);                                \
  static void name()

#define CHECK(condition)                                                                        \
  do {                                                                                          \
    if (!(condition)) anaf::TESTING::reportFailure(__FILE__, __LINE__, #condition);             \
  } while (false)

#define CHECK_MSG(condition, message)                                                           \
  do {                                                                                          \
    if (!(condition)) anaf::TESTING::reportFailure(__FILE__, __LINE__, std::string(#condition) + " -- " + (message)); \
  } while (false)

#define REQUIRE(condition)                                                                      \
  do {                                                                                          \
    if (!(condition)) {                                                                         \
      anaf::TESTING::reportFailure(__FILE__, __LINE__, #condition);                             \
      throw anaf::TESTING::RequireFailure{};                                                    \
    }                                                                                           \
  } while (false)
