#include "test_framework.h"
#include "thread_pool.h"

#include <atomic>
#include <cstddef>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

using minibuild::Task;
using minibuild::TaskID;
using minibuild::TaskState;
using minibuild::ThreadPool;

static_assert(!std::is_copy_constructible_v<ThreadPool>);
static_assert(!std::is_copy_assignable_v<ThreadPool>);

static_assert(!std::is_move_constructible_v<ThreadPool>);
static_assert(!std::is_move_assignable_v<ThreadPool>);

TEST_CASE("ThreadPool: zero workers are rejected") {
  EXPECT_THROWS_AS(ThreadPool pool(0), std::invalid_argument);
}

TEST_CASE("ThreadPool: proceeding with thread pool construction normally") {
  constexpr std::size_t worker_count = 4;

  ThreadPool pool(worker_count);

  CHECK(pool.WorkerCount() == worker_count);
  CHECK(!pool.IsStopped());

  pool.Stop();

  CHECK(pool.WorkerCount() == worker_count);
  CHECK(pool.IsStopped());
}

TEST_CASE("ThreadPool: null task is rejected") {
  ThreadPool pool(1);

  EXPECT_THROWS_AS(
    static_cast<void>(pool.Submit(nullptr)),
    std::invalid_argument);

  pool.Stop();
  CHECK(pool.IsStopped());
}

TEST_CASE("ThreadPool: a single Task can be executed") {
  std::atomic<int> count{0};

  Task task(1, "A", [&] {
    ++ count;
  });

  ThreadPool pool(1);

  CHECK(pool.Submit(&task));

  pool.Stop();
  CHECK(pool.IsStopped());

  CHECK(count.load() == 1);
  CHECK(task.GetTaskState() == TaskState::Finished);
}

TEST_CASE("all tasks have been executed") {
  constexpr std::size_t worker_count = 4;
  constexpr std::size_t task_count = 100;

  std::atomic<int> execution_count{0};

  std::vector<std::unique_ptr<Task>> tasks;
  for (std::size_t i = 0; i < task_count; ++ i) {
    tasks.push_back(std::make_unique<Task>(i, "task", [&] {
      ++ execution_count;
    }));
  }

  ThreadPool pool(worker_count);
  for (std::size_t i = 0; i < task_count; ++ i) {
    CHECK(pool.Submit(tasks[i].get()));
  }

  pool.Stop();

  CHECK(execution_count.load() == task_count);

  for (std::size_t i = 0; i < task_count; ++ i) {
    CHECK(tasks[i].get()->GetTaskState() == TaskState::Finished);
  }
}

TEST_CASE("ThreadPool: multiple workers truly work in parallel") {
  ThreadPool pool(2);

  std::promise<void> task_a_satrted;
  std::future<void> task_a_future = task_a_satrted.get_future();

  std::promise<void> task_b_satrted;
  std::future<void> task_b_future = task_b_satrted.get_future();

  std::promise<void> release;
  std::shared_future<void> release_future = release.get_future().share();

  Task task_a(1, "A", [&] {
    task_a_satrted.set_value();
    release_future.wait();
  });

  Task task_b(2, "B", [&] {
    task_b_satrted.set_value();
    release_future.wait();
  });

  CHECK(pool.Submit(&task_a));
  CHECK(pool.Submit(&task_b));

  task_a_future.wait();
  CHECK(task_a.GetTaskState() == TaskState::Running);

  task_a_future.wait();
  CHECK(task_b.GetTaskState() == TaskState::Running);

  release.set_value();

  pool.Stop();

  CHECK(task_a.GetTaskState() == TaskState::Finished);
  CHECK(task_b.GetTaskState() == TaskState::Finished);
}

TEST_CASE("ThreadPool: failed task does not kill worker") {
  ThreadPool pool(1);

  Task failed(1, "failed", [] {
    throw std::runtime_error("boom");
  });

  std::atomic<int> count{0};

  Task next(2, "next", [&] {
    ++count;
  });

  CHECK(pool.Submit(&failed));
  CHECK(pool.Submit(&next));

  pool.Stop();

  CHECK(failed.GetTaskState() == TaskState::Failed);
  CHECK(next.GetTaskState() == TaskState::Finished);
  CHECK(count.load() == 1);
}

TEST_CASE("ThreadPool: `Stop` drains submittted tasks") {
  constexpr std::size_t worker_count = 2;
  constexpr std::size_t task_count = 20;

  std::atomic<int> count{0};

  ThreadPool pool(worker_count);
  std::vector<std::unique_ptr<Task>> tasks;
  for (std::size_t i = 0; i < task_count; ++ i) {
    tasks.push_back(std::make_unique<Task>(i, "task", [&] {
      ++ count;
    }));
  }

  for (std::size_t i = 0; i < task_count; ++ i) {
    CHECK(pool.Submit(tasks[i].get()));
  }

  pool.Stop();

  CHECK(count.load() == 20);
}

TEST_CASE("ThreadPool: `Submi`t is rejected after Stop") {
  ThreadPool pool(1);
  pool.Stop();

  Task task(1, "task", [] {

  });

  CHECK(!pool.Submit(&task));
  CHECK(task.GetTaskState() == TaskState::Pending);
}

TEST_CASE("ThreadPool: `Stop` is idempotent") {
  ThreadPool pool(2);

  pool.Stop();
  pool.Stop();
  pool.Stop();

  CHECK(pool.IsStopped());
}

TEST_CASE("ThreadPool: concurrent Stop is safe") {
  using namespace std::chrono_literals;

  ThreadPool pool(1);

  std::promise<void> task_started_promise;
  std::future<void> task_started_future = task_started_promise.get_future();

  std::promise<void> release_task_promise;
  std::shared_future<void> release_task_future = release_task_promise.get_future().share();

  Task task(1, "blocking-task", [&] {
    task_started_promise.set_value();
    release_task_future.wait();
  });

  CHECK(pool.Submit(&task));

  task_started_future.wait();

  constexpr std::size_t stopper_count = 4;

  std::promise<void> start_promise;
  std::shared_future<void> start_future = start_promise.get_future().share();

  std::atomic<std::size_t> ready_count{0};
  std::atomic<std::size_t> returned_count{0};
  std::atomic<std::size_t> unexpected_error_count{0};

  std::vector<std::thread> stoppers;
  stoppers.reserve(stopper_count);

  for (std::size_t i = 0; i < stopper_count; ++i) {
    stoppers.emplace_back([&] {
      ++ready_count;

      start_future.wait();

      try {
        pool.Stop();

        ++returned_count;
      } catch (...) {
        ++unexpected_error_count;
      }
    });
  }

  while (ready_count.load() != stopper_count) {
    std::this_thread::yield();
  }

  start_promise.set_value();

  const auto deadline = std::chrono::steady_clock::now() + 1s;

  while (!pool.IsStopped() &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }

  const bool stop_started = pool.IsStopped();

  const bool all_stops_are_waiting =
      returned_count.load() == 0;

  release_task_promise.set_value();

 for (auto& stopper : stoppers) {
    if (stopper.joinable()) {
      stopper.join();
    }
  }

  CHECK(stop_started);
  CHECK(all_stops_are_waiting);
  CHECK(returned_count.load() == stopper_count);
  CHECK(unexpected_error_count.load() == 0);
  CHECK(pool.IsStopped());
  CHECK(task.GetTaskState() == TaskState::Finished);
}

TEST_CASE("ThreadPool: destructor performs graceful shutdown") {
  std::atomic<int> count{0};

  Task a(1, "a", [&] { ++ count; });
  Task b(2, "b", [&] { ++ count; });

  {
    ThreadPool pool(2);

    CHECK(pool.Submit(&a));
    CHECK(pool.Submit(&b));
  }

  CHECK(count.load() == 2);
  CHECK(a.GetTaskState() == TaskState::Finished);
  CHECK(b.GetTaskState() == TaskState::Finished);
}

TEST_CASE("ThreadPool: completed task is reported") {
  ThreadPool pool(1);

  Task task(1, "task", [] {});

  CHECK(pool.Submit(&task));

  Task* completed = nullptr;

  const bool popped = pool.WaitAndPopCompleted(completed);

  pool.Stop();

  CHECK(popped);
  CHECK(completed == &task);
  CHECK(task.GetTaskState() == TaskState::Finished);
}


TEST_CASE("ThreadPool: failed task is reported as completed") {
  ThreadPool pool(1);

  Task task(1, "failed", [] {
    throw std::runtime_error("boom");
  });

  CHECK(pool.Submit(&task));

  Task* completed = nullptr;
  const bool popped = pool.WaitAndPopCompleted(completed);

  pool.Stop();

  CHECK(popped);
  CHECK(completed == &task);
  CHECK(task.GetTaskState() == TaskState::Failed);
}

TEST_CASE("ThreadPool: multiple tasks produce exactly one completion each") {
  constexpr std::size_t task_count = 50;
  constexpr std::size_t worker_count = 5;

  ThreadPool pool(worker_count);

  std::vector<std::unique_ptr<Task>> tasks;
  tasks.reserve(task_count);

  for (std::size_t i = 0; i < task_count; ++i) {
    tasks.push_back(
        std::make_unique<Task>(
            i + 1,
            "task",
            [] {}));
  }

  // Submit all tasks, not just worker_count tasks.
  for (std::size_t i = 0; i < task_count; ++i) {
    CHECK(pool.Submit(tasks[i].get()));
  }

  // Drain all submitted work and close completed_queue_.
  pool.Stop();

  std::unordered_set<TaskID> completed_ids;
  std::size_t completion_count = 0;

  Task* completed = nullptr;

  while (pool.WaitAndPopCompleted(completed)) {
    ++completion_count;

    if (completed != nullptr) {
      completed_ids.insert(completed->GetTaskID());
    }
  }

  CHECK(completion_count == task_count);
  CHECK(completed_ids.size() == task_count);

  for (const auto& task : tasks) {
    CHECK(
        task->GetTaskState() ==
        TaskState::Finished);
  }
}

TEST_CASE("ThreadPool: `Stop` closes completion queue after draining workers") {
  ThreadPool pool(2);

  Task a(1, "A", [] {});
  Task b(2, "B", [] {});
  Task c(3, "C", [] {});

  CHECK(pool.Submit(&a));
  CHECK(pool.Submit(&b));
  CHECK(pool.Submit(&c));

  pool.Stop();

  std::vector<Task*> completed;

  Task* task = nullptr;

  while (pool.WaitAndPopCompleted(task)) {
    completed.push_back(task);
  }

  CHECK(completed.size() == 3);

  CHECK(a.GetTaskState() == TaskState::Finished);
  CHECK(b.GetTaskState() == TaskState::Finished);
  CHECK(c.GetTaskState() == TaskState::Finished);
}

}  // namespace
