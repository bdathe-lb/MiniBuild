#include "task.h"
#include "task_graph.h"
#include "test_framework.h"

namespace {

using minibuild::Task;
using minibuild::TaskState;
using minibuild::TaskGraph;
using minibuild::TaskID;

static_assert(std::is_default_constructible_v<TaskGraph>);

static_assert(!std::is_copy_constructible_v<TaskGraph>);
static_assert(!std::is_copy_assignable_v<TaskGraph>);

static_assert(!std::is_move_constructible_v<TaskGraph>);
static_assert(!std::is_move_assignable_v<TaskGraph>);

TEST_CASE("TaskGraph: empty graph") {
  TaskGraph graph;

  CHECK(graph.Empty());
  CHECK(graph.Size() == 0);
  CHECK(graph.FindTask(0) == nullptr);
  CHECK(graph.FindTask(1) == nullptr);
  CHECK(graph.GetReadyTasks().empty());
}

TEST_CASE("TaskGraph: add and fina a task") {
  TaskGraph graph;

  const TaskID id = graph.AddTask("Compile", [] {});

  CHECK(id == 1);
  CHECK(graph.Size() == 1);
  CHECK(!graph.Empty());

  Task* task = graph.FindTask(id);

  CHECK(task != nullptr);
  CHECK(task->GetTaskID() == id);
  CHECK(task->GetTaskName() == "Compile");
  CHECK(task->GetTaskState() == TaskState::Pending);
}

TEST_CASE("TaskGraph: assign unique sequential task IDs") {
  TaskGraph graph;

  const TaskID a = graph.AddTask("A", [] {});
  const TaskID b = graph.AddTask("B", [] {});
  const TaskID c = graph.AddTask("C", [] {});

  CHECK(a == 1);
  CHECK(b == 2);
  CHECK(c == 3);

  CHECK(a != b);
  CHECK(b != c);
  CHECK(a != c);

  CHECK(graph.Size() == 3);
}

TEST_CASE("TaskGraph: allow duplicate task names") {
  TaskGraph graph;

  const TaskID a = graph.AddTask("Compile", [] {});
  const TaskID b = graph.AddTask("Compile", [] {});

  CHECK(a != b);

  CHECK(graph.FindTask(a)->GetTaskName() == "Compile");
  CHECK(graph.FindTask(b)->GetTaskName() == "Compile");
}

TEST_CASE("TaskGraph: failed task creation does not consume ID") {
  TaskGraph graph;

  EXPECT_THROWS_AS(
    static_cast<void>(graph.AddTask("", [] {})),
    std::invalid_argument);

  const TaskID id = graph.AddTask("Compile", [] {});

  CHECK(id == 1);
  CHECK(graph.Size() == 1);
}

TEST_CASE("TaskGraph: tasks without dependencies are ready") {
  TaskGraph graph;

  const TaskID a = graph.AddTask("A", [] {});
  const TaskID b = graph.AddTask("B", [] {});
  const TaskID c = graph.AddTask("C", [] {});

  const std::vector<Task*> ready = graph.GetReadyTasks();

  CHECK(ready.size() == 3);
  CHECK(ready[0]->GetTaskID() == a);
  CHECK(ready[1]->GetTaskID() == b);
  CHECK(ready[2]->GetTaskID() == c);
}

TEST_CASE("TaskGraph: finished task is not ready") {
  TaskGraph graph;

  const TaskID id = graph.AddTask("Compile", [] {});

  Task* task = graph.FindTask(id);
  CHECK(task != nullptr);

  CHECK(graph.GetReadyTasks().size() == 1);

  task->Execute();

  CHECK(task->GetTaskState() == TaskState::Finished);
  CHECK(graph.GetReadyTasks().empty());
}

TEST_CASE("TaskGraph: failed task is not ready") {
  TaskGraph graph;

  const TaskID id = graph.AddTask("Compile", [] {
        throw std::runtime_error("compile failed");
      });

  Task* task = graph.FindTask(id);

  EXPECT_THROWS_AS(
      task->Execute(),
      std::runtime_error);

  CHECK(task->GetTaskState() == TaskState::Failed);
  CHECK(graph.GetReadyTasks().empty());
}

TEST_CASE("TaskGraph: single dependency test") {
  // A → B
  TaskGraph graph;

  const TaskID a = graph.AddTask("A", [] {});
  const TaskID b = graph.AddTask("B", [] {});

  graph.AddDependency(a, b);

  auto ready = graph.GetReadyTasks();

  CHECK(ready.size() == 1);
  CHECK(ready[0]->GetTaskID() == a);

  graph.FindTask(a)->Execute();

  ready = graph.GetReadyTasks();

  CHECK(ready.size() == 1);
  CHECK(ready[0]->GetTaskID() == b);
}

TEST_CASE("TaskGraph: multiple pre-dependencies") {
  // A ─┐
  //    ├──> C
  // B ─┘
  TaskGraph graph;

  const TaskID a = graph.AddTask("A", [] {});
  const TaskID b = graph.AddTask("B", [] {});
  const TaskID c = graph.AddTask("C", [] {});

  graph.AddDependency(a, c);
  graph.AddDependency(b, c);

  auto ready = graph.GetReadyTasks();

  CHECK(ready.size() == 2);
  CHECK(ready[0]->GetTaskID() == a);
  CHECK(ready[1]->GetTaskID() == b);

  graph.FindTask(a)->Execute();

  ready = graph.GetReadyTasks();

  CHECK(ready.size() == 1);
  CHECK(ready[0]->GetTaskID() == b);

  graph.FindTask(b)->Execute();

  ready = graph.GetReadyTasks();

  CHECK(ready.size() == 1);
  CHECK(ready[0]->GetTaskID() == c);
}

TEST_CASE("TaskGraph: chained dependencies") {
  // A → B → C
  TaskGraph graph;

  const TaskID a = graph.AddTask("A", [] {});
  const TaskID b = graph.AddTask("B", [] {});
  const TaskID c = graph.AddTask("C", [] {});

  graph.AddDependency(a, b);
  graph.AddDependency(b, c);

  auto ready = graph.GetReadyTasks();

  CHECK(ready.size() == 1);
  CHECK(ready[0]->GetTaskID() == a);

  graph.FindTask(a)->Execute();

  ready = graph.GetReadyTasks();

  CHECK(ready.size() == 1);
  CHECK(ready[0]->GetTaskID() == b);

  graph.FindTask(b)->Execute();

  ready = graph.GetReadyTasks();

  CHECK(ready.size() == 1);
  CHECK(ready[0]->GetTaskID() == c);

  graph.FindTask(c)->Execute();

  ready = graph.GetReadyTasks();

  CHECK(ready.size() == 0);
}

TEST_CASE("TaskGraph: dependency failure blocks successors") {
  // A -> B
  TaskGraph graph;

  const TaskID a = graph.AddTask("A", [] {
    throw std::runtime_error("compile failed");
  });
  const TaskID b = graph.AddTask("B", [] {});

  graph.AddDependency(a, b);

  EXPECT_THROWS_AS(
    graph.FindTask(a)->Execute(),
    std::runtime_error);

  CHECK(graph.FindTask(a)->GetTaskState()
      == TaskState::Failed);

  CHECK(graph.FindTask(b)->GetTaskState()
      == TaskState::Pending);

  CHECK(graph.GetReadyTasks().empty());
}

TEST_CASE("TaskGraph: illegal ID") {
  TaskGraph graph;

  const TaskID a = graph.AddTask("A", [] {});
  const TaskID b = graph.AddTask("B", [] {});
  const TaskID c = graph.AddTask("C", [] {});

  graph.AddDependency(a, b);
  graph.AddDependency(b, c);

  EXPECT_THROWS_AS(
    graph.AddDependency(999, a),
    std::out_of_range);

  EXPECT_THROWS_AS(
    graph.AddDependency(a, 999),
    std::out_of_range);

  CHECK(graph.Size() == 3);
}

TEST_CASE("TaskGraph: self-dependency") {
  TaskGraph graph;

  const TaskID a = graph.AddTask("A", [] {});

  EXPECT_THROWS_AS(
    graph.AddDependency(a, a),
    std::invalid_argument);

  const auto ready = graph.GetReadyTasks();

  CHECK(ready.size() == 1);
  CHECK(ready[0]->GetTaskID() == a);
}

TEST_CASE("TaskGraph: duplicate edges") {
  TaskGraph graph;

  const TaskID a = graph.AddTask("A", [] {});
  const TaskID b = graph.AddTask("B", [] {});

  graph.AddDependency(a, b);

  EXPECT_THROWS_AS(
    graph.AddDependency(a, b),
    std::logic_error);

  graph.FindTask(a)->Execute();

  const auto ready = graph.GetReadyTasks();

  CHECK(ready.size() == 1);
  CHECK(ready[0]->GetTaskID() == b);
}

TEST_CASE("TaskGraph: direct ring") {
  TaskGraph graph;

  const TaskID a = graph.AddTask("A", [] {});
  const TaskID b = graph.AddTask("B", [] {});

  graph.AddDependency(a, b);

  EXPECT_THROWS_AS(
      graph.AddDependency(b, a),
      std::logic_error);
}

TEST_CASE("TaskGraph: indirect ring") {
  TaskGraph graph;

  const TaskID a = graph.AddTask("A", [] {});
  const TaskID b = graph.AddTask("B", [] {});
  const TaskID c = graph.AddTask("C", [] {});

  graph.AddDependency(a, b);
  graph.AddDependency(b, c);

  EXPECT_THROWS_AS(
      graph.AddDependency(c, a),
      std::logic_error);

  auto ready = graph.GetReadyTasks();

  CHECK(ready.size() == 1);
  CHECK(ready[0]->GetTaskID() == a);

  graph.FindTask(a)->Execute();

  ready = graph.GetReadyTasks();

  CHECK(ready.size() == 1);
  CHECK(ready[0]->GetTaskID() == b);

  graph.FindTask(b)->Execute();

  ready = graph.GetReadyTasks();

  CHECK(ready.size() == 1);
  CHECK(ready[0]->GetTaskID() == c);

  graph.FindTask(c)->Execute();

  ready = graph.GetReadyTasks();

  CHECK(ready.size() == 0);
}

TEST_CASE("TaskGraph: query ready tasks through const graph") {
  TaskGraph graph;

  const TaskID a =
      graph.AddTask("A", [] {});

  const TaskGraph& const_graph = graph;

  const std::vector<const Task*> ready =
      const_graph.GetReadyTasks();

  CHECK(ready.size() == 1);
  CHECK(ready[0]->GetTaskID() == a);

  const Task* task =
      const_graph.FindTask(a);

  CHECK(task != nullptr);
  CHECK(task->GetTaskID() == a);
}

} // namespace
