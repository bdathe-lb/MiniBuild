#include "task.h"
#include "test_framework.h"

#include <atomic>
#include <cstddef>
#include <functional>
#include <future>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using minibuild::Task;
using minibuild::TaskState;
using namespace std::chrono_literals;

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

TEST_CASE("Task: concurrent Execute runs action only once") {
  std::atomic<int> execution_count{0};
  std::atomic<int> success_count{0};
  std::atomic<int> logic_error_count{0};
  std::atomic<std::size_t> ready_count{0};

  constexpr std::size_t thread_count = 2;

  // satrt signal
  std::promise<void> start_promise;
  std::shared_future<void> start_future = 
    start_promise.get_future().share();

  Task task(1, "A", [&] {
    ++ execution_count;

    std::this_thread::sleep_for(50ms);
  });

  std::vector<std::thread> threads;
  threads.reserve(thread_count);

  for (std::size_t i = 0; i < thread_count; ++ i) {
    threads.emplace_back( [&] {
      ++ ready_count;

      start_future.wait();

      try {
        task.Execute();
        ++ success_count;
      } catch (const std::logic_error&) {
        ++ logic_error_count;
      }
    });
  }

  while (ready_count.load() != thread_count) {
    std::this_thread::yield();
  }

  start_promise.set_value();

  for (auto& thread : threads) {
    if (thread.joinable()) 
      thread.join();
  }

  CHECK(execution_count == 1);
  CHECK(success_count == 1);
  CHECK(logic_error_count == 1);
  CHECK(task.GetTaskState() == TaskState::Finished);
}

TEST_CASE("Task: Running state is observable during execution") {
  std::promise<void> action_started_promise;
  std::future<void> action_started_future = 
    action_started_promise.get_future();

  std::promise<void> allow_finish_promise;
  std::shared_future<void> allow_finish_future = 
    allow_finish_promise.get_future().share();

  Task task(1, "task", [&] {
    action_started_promise.set_value();
    allow_finish_future.wait();
  });

  std::thread worker([&] {
    task.Execute(); 
  });

  action_started_future.wait();
  const TaskState observed_state = task.GetTaskState();
  allow_finish_promise.set_value();

  worker.join();

  CHECK(observed_state == TaskState::Running);
  CHECK(task.GetTaskState() == TaskState::Finished);
}

TEST_CASE("Task: concurrent failed Execute runs action only once") {
  constexpr std::size_t thread_count = 8;

  std::atomic<int> execution_count{0};
  std::atomic<int> runtime_error_count{0};
  std::atomic<int> logic_error_count{0};

  Task task(1, "failed-task", [&] {
    ++ execution_count;

    throw std::runtime_error("task failed");
  });

  std::promise<void> action_started_promise;
  std::future<void> action_started_future = 
    action_started_promise.get_future();
  std::atomic<std::size_t> ready_count{0};

  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (std::size_t i = 0; i < thread_count; ++ i) {
    threads.emplace_back([&] {
      ++ ready_count;
      action_started_future.wait();

      try {
        task.Execute();
      } catch (const std::logic_error&) {
        ++ logic_error_count;
      } catch (const std::runtime_error&) {
        ++ runtime_error_count;
      }
    });
  }

  while (ready_count.load() != thread_count) {
    std::this_thread::yield();
  }

  action_started_promise.set_value();

  for (auto& thread : threads) {
    if (thread.joinable())
      thread.join();
  }

  CHECK(execution_count == 1);
  CHECK(runtime_error_count == 1);
  CHECK(logic_error_count == thread_count - 1);
  CHECK(task.GetTaskState() == TaskState::Failed);
}

TEST_CASE("Task: multithreaded race condition testing") {
  constexpr std::size_t thread_count = 16;

  std::atomic<int> execution_count{0};
  std::atomic<int> success_count{0};
  std::atomic<int> logic_error_count{0};
  std::atomic<std::size_t> ready_count{0};

  Task task(1, "failed-task", [&] {
    ++ execution_count;
    std::this_thread::sleep_for(20ms);
  });

  std::promise<void> action_started_promise;
  std::future<void> action_started_future = 
    action_started_promise.get_future();

  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (std::size_t i = 0; i < thread_count; ++ i) {
    threads.emplace_back([&] {
      ++ ready_count;
      action_started_future.wait();

      try {
        task.Execute();
        ++ success_count;
      } catch (const std::logic_error&) {
        ++ logic_error_count;
      }
    });
  }

  while (ready_count.load() != thread_count) {
    std::this_thread::yield();
  }

  action_started_promise.set_value();

  for (auto& thread : threads) {
    if (thread.joinable())
      thread.join();
  }

  CHECK(execution_count.load() == 1);
  CHECK(success_count.load() == 1);
  CHECK(logic_error_count.load()
        == static_cast<int>(thread_count - 1));
  CHECK(task.GetTaskState() == TaskState::Finished);
}

} // namespace
