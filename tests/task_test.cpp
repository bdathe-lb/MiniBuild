#include "task.h"
#include "test_framework.h"

#include <cstddef>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {

using minibuild::Task;
using minibuild::TaskState;

static_assert(!std::is_default_constructible_v<Task>);
static_assert(!std::is_copy_constructible_v<Task>);
static_assert(!std::is_copy_assignable_v<Task>);
static_assert(!std::is_move_constructible_v<Task>);
static_assert(!std::is_move_assignable_v<Task>);

TEST_CASE("Task: valid construction") {
  Task task(1, "CompileA", [] {});

  CHECK(task.GetTaskID() == 1);
  CHECK(task.GetTaskName() == "CompileA");
  CHECK(task.GetTaskState() == TaskState::Pending);
}

TEST_CASE("Task: empty name is rejected") {
  const std::string message =
      EXPECT_THROWS_AS(
          Task task(2, "", [] {}),
          std::invalid_argument);

  CHECK(message == "Task name cannot be empty");
}

TEST_CASE("Task: empty action is rejected") {
  const std::string message =
      EXPECT_THROWS_AS(
          Task task(3, "CompileC", std::function<void()>{}),
          std::invalid_argument);

  CHECK(message == "Task action cannot be empty");
}

TEST_CASE("Task: successful execution") {
  std::size_t execution_count = 0;

  Task task(4, "CompileD", [&execution_count] {
    ++execution_count;
  });

  task.Execute();

  CHECK(execution_count == 1);
  CHECK(task.GetTaskState() == TaskState::Finished);
}

TEST_CASE("Task: state is Running inside action") {
  Task* observed_task = nullptr;
  TaskState observed_state = TaskState::Pending;

  Task task(5, "Link", [&] {
    observed_state = observed_task->GetTaskState();
  });

  observed_task = &task;
  task.Execute();

  CHECK(observed_state == TaskState::Running);
  CHECK(task.GetTaskState() == TaskState::Finished);
}

TEST_CASE("Task: failure updates state and rethrows original exception") {
  Task task(6, "CompileFailed", [] {
    throw std::runtime_error("compile failed");
  });

  const std::string message =
      EXPECT_THROWS_AS(task.Execute(), std::runtime_error);

  CHECK(message == "compile failed");
  CHECK(task.GetTaskState() == TaskState::Failed);
}

TEST_CASE("Task: Finished task cannot execute again") {
  std::size_t execution_count = 0;

  Task task(7, "Package", [&execution_count] {
    ++execution_count;
  });

  task.Execute();

  const std::string message =
      EXPECT_THROWS_AS(task.Execute(), std::logic_error);

  CHECK(message == "Task can only be executed from Pending state");
  CHECK(execution_count == 1);
  CHECK(task.GetTaskState() == TaskState::Finished);
}

TEST_CASE("Task: Failed task cannot execute again") {
  std::size_t execution_count = 0;

  Task task(8, "AlwaysFails", [&execution_count] {
    ++execution_count;
    throw std::runtime_error("first failure");
  });

  EXPECT_THROWS_AS(task.Execute(), std::runtime_error);

  const std::string message =
      EXPECT_THROWS_AS(task.Execute(), std::logic_error);

  CHECK(message == "Task can only be executed from Pending state");
  CHECK(execution_count == 1);
  CHECK(task.GetTaskState() == TaskState::Failed);
}

TEST_CASE("Task: stream output") {
  Task task(9, "CompileStream", [] {});

  std::ostringstream output;
  output << task;

  CHECK(output.str() ==
        "Task [9] {CompileStream} - State: Pending");
}

}  // namespace
