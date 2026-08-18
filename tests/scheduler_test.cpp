#include "test_framework.h"
#include "scheduler.h"

#include <chrono>
#include <stdexcept>
#include <thread>

namespace {

using minibuild::TaskID;
using minibuild::TaskState;
using minibuild::TaskGraph;
using minibuild::Scheduler;
using namespace std::chrono_literals;

static_assert(!std::is_copy_constructible_v<Scheduler>);
static_assert(!std::is_copy_assignable_v<Scheduler>);

static_assert(!std::is_move_constructible_v<Scheduler>);
static_assert(!std::is_move_assignable_v<Scheduler>);

TEST_CASE("Scheduler: empty graph succeeds") {
  TaskGraph graph; 

  CHECK(graph.Size() == 0);

  Scheduler scheduler(graph, 1);

  CHECK(scheduler.Run() == true);
}

TEST_CASE("Scheduler: executes single task successfully") {
  TaskGraph graph;

  bool executed = false;
  TaskID id = graph.AddTask("A", [&] {
    executed = true; 
  });

  Scheduler scheduler(graph, 1);
  
  CHECK(scheduler.Run());
  CHECK(graph.FindTask(id)->GetTaskState() == TaskState::Finished);
  CHECK(executed == true);
}

TEST_CASE("Scheduler: executes linear dependency chain") {
  TaskGraph graph;

  int count = 0;
  TaskID id_a = graph.AddTask("A", [&] { ++ count; });
  TaskID id_b = graph.AddTask("B", [&] { ++ count; });
  TaskID id_c = graph.AddTask("C", [&] { ++ count; });

  graph.AddDependency(id_a, id_b);
  graph.AddDependency(id_b, id_c);

  Scheduler scheduler(graph, 2);

  CHECK(scheduler.Run());

  CHECK(graph.FindTask(id_a)->GetTaskState() == TaskState::Finished);
  CHECK(graph.FindTask(id_b)->GetTaskState() == TaskState::Finished);
  CHECK(graph.FindTask(id_c)->GetTaskState() == TaskState::Finished);

  CHECK(count == 3);
}

TEST_CASE("Scheduler: executes independent tasks concurrently") {
  TaskGraph graph;

  double sequential_time_threshold = 300;
  TaskID id_a = graph.AddTask("A", [&] { std::this_thread::sleep_for(100ms); });
  TaskID id_b = graph.AddTask("B", [&] { std::this_thread::sleep_for(100ms); });
  TaskID id_c = graph.AddTask("C", [&] { std::this_thread::sleep_for(100ms); });

  Scheduler scheduler(graph, 3);

  auto start = std::chrono::steady_clock::now();
  CHECK(scheduler.Run());
  auto end = std::chrono::steady_clock::now();

  auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  double duration_ms = duration_us.count() / 1000.0;

  CHECK(graph.FindTask(id_a)->GetTaskState() == TaskState::Finished);
  CHECK(graph.FindTask(id_b)->GetTaskState() == TaskState::Finished);
  CHECK(graph.FindTask(id_c)->GetTaskState() == TaskState::Finished);

  CHECK(duration_ms < sequential_time_threshold);
}

TEST_CASE("Scheduler: failed task stops downstream execution") {
  TaskGraph graph;

  TaskID id_a = graph.AddTask("A", [] {
    throw std::runtime_error("failed");
  });

  bool executed = false;
  TaskID id_b = graph.AddTask("B", [&] {
    executed = true; 
  });

  graph.AddDependency(id_a, id_b);

  Scheduler scheduler(graph, 2);

  CHECK(scheduler.Run() == false);
  CHECK(graph.FindTask(id_a)->GetTaskState() == TaskState::Failed);
  CHECK(graph.FindTask(id_b)->GetTaskState() == TaskState::Pending);
  CHECK(executed == false);
}

TEST_CASE("Scheduler: failed task does not cancel already submitted tasks") {
  TaskGraph graph;

  bool executed = false;
  TaskID id_a = graph.AddTask("A", [] { 
    throw std::runtime_error("failed"); 
  });

  TaskID id_b = graph.AddTask("B", [] {});

  TaskID id_c = graph.AddTask("C", [&] {
    std::this_thread::sleep_for(500ms); 
    executed = true;
  });

  graph.AddDependency(id_a, id_b);

  Scheduler scheduler(graph, 2);

  CHECK(scheduler.Run() == false);

  CHECK(graph.FindTask(id_a)->GetTaskState() == TaskState::Failed);
  CHECK(graph.FindTask(id_b)->GetTaskState() == TaskState::Pending);
  CHECK(graph.FindTask(id_c)->GetTaskState() == TaskState::Finished);

  CHECK(executed == true);
}

TEST_CASE("Scheduler: diamond dependency graph") {
  TaskGraph graph;

  TaskID id_a = graph.AddTask("A", [&] {});
  TaskID id_b = graph.AddTask("B", [&] {});
  TaskID id_c = graph.AddTask("C", [&] {});
  TaskID id_d = graph.AddTask("D", [&] {});

  graph.AddDependency(id_a, id_b);
  graph.AddDependency(id_a, id_c);
  graph.AddDependency(id_b, id_d);
  graph.AddDependency(id_c, id_d);

  Scheduler scheduler(graph, 2);

  CHECK(scheduler.Run());
  CHECK(graph.FindTask(id_a)->GetTaskState() == TaskState::Finished);
  CHECK(graph.FindTask(id_b)->GetTaskState() == TaskState::Finished);
  CHECK(graph.FindTask(id_c)->GetTaskState() == TaskState::Finished);
  CHECK(graph.FindTask(id_d)->GetTaskState() == TaskState::Finished);
}

TEST_CASE("Scheduler: Run can only be called once") {
  TaskGraph graph;

  TaskID id = graph.AddTask("A", [] { std::this_thread::sleep_for(1000ms); });

  Scheduler scheduler(graph, 2);

  CHECK(scheduler.Run());

  EXPECT_THROWS_AS(static_cast<void>(scheduler.Run()), std::logic_error);
  
  CHECK(graph.FindTask(id)->GetTaskState() == TaskState::Finished);
}

} // namespace
