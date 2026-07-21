#ifndef MINI_BUILD_TEST_FRAMEWORK_H
#define MINI_BUILD_TEST_FRAMEWORK_H

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace minibuild::test {

class TestFailure : public std::runtime_error {
public:
  // Represents inheriting the parent class constructor
  // Inherit the parent class constructor into the child class
  using std::runtime_error::runtime_error;
};

using TestFunction = void (*)();

struct TestCase {
  std::string name;
  TestFunction function;
};

std::vector<TestCase>& GetTestRegistry();

class TestRegistrar {
public:
  TestRegistrar(const char* name, TestFunction function);
};

[[noreturn]] void Fail(const char* expression,
                       const char* file,
                       int line,
                       const std::string& detail = {});

int RunAllTests();

template <typename ExpectedException, typename Function>
std::string ExpectThrows(Function&& function,
                         const char* expected_exception_name,
                         const char* file,
                         int line) {
  try {
    std::forward<Function>(function)();
  } catch (const ExpectedException& exception) {
    return exception.what();
  } catch (const std::exception& exception) {
    Fail(expected_exception_name,
         file,
         line,
         std::string("caught a different std::exception: ") +
             exception.what());
  } catch (...) {
    Fail(expected_exception_name,
         file,
         line,
         "caught a non-standard exception");
  }

  Fail(expected_exception_name,
       file,
       line,
       "no exception was thrown");
}

}  // namespace minibuild::test

#define MB_TEST_CONCAT_IMPL(left, right) left##right
#define MB_TEST_CONCAT(left, right) MB_TEST_CONCAT_IMPL(left, right)

#define MB_TEST_CASE_IMPL(name, line)                                        \
  static void MB_TEST_CONCAT(MiniBuildTestFunction_, line)();                \
  static ::minibuild::test::TestRegistrar                                    \
      MB_TEST_CONCAT(MiniBuildTestRegistrar_, line)(                         \
          name, &MB_TEST_CONCAT(MiniBuildTestFunction_, line));              \
  static void MB_TEST_CONCAT(MiniBuildTestFunction_, line)()

#define TEST_CASE(name) MB_TEST_CASE_IMPL(name, __LINE__)

#define CHECK(expression)                                                     \
  do {                                                                        \
    if (!(expression)) {                                                      \
      ::minibuild::test::Fail(#expression, __FILE__, __LINE__);               \
    }                                                                         \
  } while (false)

#define EXPECT_THROWS_AS(expression, exception_type)                          \
  ::minibuild::test::ExpectThrows<exception_type>(                            \
      [&] { expression; }, #exception_type, __FILE__, __LINE__)

#endif  // MINI_BUILD_TEST_FRAMEWORK_H
