#ifndef MINI_BUILD_TASK_GRAPH_H
#define MINI_BUILD_TASK_GRAPH_H

#include "task.h"

#include <cstddef>
#include <memory>
#include <vector>
#include <string>

namespace minibuild {

class TaskGraph {
private:
  struct TaskNode {
    std::unique_ptr<Task> task;
    std::vector<TaskID> dependencies; // Who do I depend on
    std::vector<TaskID> dependents;   // Who depends on me
  };

  std::vector<TaskNode> nodes_;
  TaskID next_id_{1};

public:
  /// Default constructor
  TaskGraph() = default;

  // Prohibit copying
  TaskGraph(const TaskGraph&) = delete;
  TaskGraph& operator=(const TaskGraph&) = delete;

  // Prohibit moving
  TaskGraph(TaskGraph&&) = delete;
  TaskGraph& operator=(TaskGraph&&) = delete;

  /// Add a task into task graph
  [[nodiscard]]
  TaskID AddTask(std::string name, TaskAction action);

  /// Add the edge
  ///   dependency -> dependent
  /// For example, AddDependency(compile, link) means that Link depends on Compile.
  void AddDependency(TaskID dependency, TaskID dependent);

  /// Find a task by task id
  [[nodiscard]]
  Task* FindTask(TaskID id) noexcept;

  [[nodiscard]]
  const Task* FindTask(TaskID id) const noexcept;

  /// Get an array of tasks with status ready
  [[nodiscard]]
  std::vector<Task*> GetReadyTasks();

  [[nodiscard]]
  std::vector<const Task*> GetReadyTasks() const;

  /// Get the number of tasks
  [[nodiscard]]
  std::size_t Size() const noexcept;

  /// Whether the task in the current task graph is empty
  [[nodiscard]]
  bool Empty() const noexcept;

private:
  /// Find a node by tasks id
  [[nodiscard]]
  TaskNode* FindNode(TaskID id) noexcept;

  [[nodiscard]]
  const TaskNode* FindNode(TaskID id) const noexcept;

  /// Whether the task status is ready
  [[nodiscard]]
  bool IsTaskReady(const TaskNode& node) const;

  /// DFS
  [[nodiscard]]
  bool HasPath(TaskID from, TaskID to) const;

  [[nodiscard]]
  bool HasPathImpl(TaskID current, TaskID target, std::vector<bool>& visited) const;
};

}  // namespace minibuild

#endif  // MINI_BUILD_TASK_GRAPH_H
