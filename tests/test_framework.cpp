#include "test_framework.h"

#include <cstddef>
#include <exception>
#include <iostream>
#include <sstream>

namespace minibuild::test {

std::vector<TestCase>& GetTestRegistry() {
  // static:
  // - Initialization occurs on the first call to GetTestRegistry().  
  // - Created only once, with lifetime spanning the entire program.  
  // - Each call to GetTestRegistry() returns the same instance.
  static std::vector<TestCase> tests;
  return tests;
}

TestRegistrar::TestRegistrar(const char* name, TestFunction function) {
  GetTestRegistry().push_back(TestCase{name, function});
}

[[noreturn]] void Fail(const char* expression,
                       const char* file,
                       int line,
                       const std::string& detail) {
  std::ostringstream message;
  message << file << ':' << line << ": check failed: " << expression;

  if (!detail.empty()) {
    message << " (" << detail << ')';
  }

  throw TestFailure(message.str());
}

int RunAllTests() {
  const std::vector<TestCase>& tests = GetTestRegistry();
  std::size_t passed = 0;

  for (const TestCase& test : tests) {
    try {
      test.function();
      ++passed;
      std::cout << "[PASS] " << test.name << '\n';
    } catch (const std::exception& exception) {
      std::cerr << "[FAIL] " << test.name << '\n'
                << "       " << exception.what() << '\n';
    } catch (...) {
      std::cerr << "[FAIL] " << test.name << '\n'
                << "       unknown exception\n";
    }
  }

  std::cout << '\n'
            << passed << '/' << tests.size() << " tests passed.\n";

  return passed == tests.size() ? 0 : 1;
}

}  // namespace minibuild::test
