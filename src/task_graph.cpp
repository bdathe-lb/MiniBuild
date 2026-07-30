#include "task_graph.h"
#include "task.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <cstddef>
#include <utility>
#include <stdexcept>
#include <vector>

namespace minibuild {

/**
 * Return the number of nodes in the task graph.
 *
 * @return The number of nodes in the graph.
 */
std::size_t TaskGraph::Size() const noexcept {
  return nodes_.size();
}

/**
 * Check whether the task graph has no nodes.
 *
 * @return true if the task graph is empty, false otherwise.
 */
bool TaskGraph::Empty() const noexcept {
  return nodes_.empty();
}

/**
 * Find a task node by its ID.
 *
 * @param id The ID of the task node to find.
 * @return Pointer to the task node if found, otherwise nullptr.
 */
TaskGraph::TaskNode* TaskGraph::FindNode(TaskID id) noexcept {
  if (id == 0 || id > nodes_.size()) {
    return nullptr;
  }

  const auto index = static_cast<std::size_t>(id - 1);
  return &nodes_[index];
}

/**
 * Find a task node by its ID.
 *
 * @param id The ID of the task node to find.
 * @return Pointer to the task node if found, otherwise nullptr.
 */
const TaskGraph::TaskNode* TaskGraph::FindNode(TaskID id) const noexcept {
  if (id == 0 || id > nodes_.size()) {
    return nullptr;
  }

  const auto index = static_cast<std::size_t>(id - 1);
  return &nodes_[index];
}

/**
 * Find the task with the given ID.
 *
 * @param id The unique identifier of the task to look up.
 * @return A pointer to the Task object if found, or nullptr if the ID is invalid or out of range.
 */
Task* TaskGraph::FindTask(TaskID id) noexcept {
  TaskNode* node = FindNode(id);

  if (node == nullptr) {
    return nullptr;
  }

  return node->task.get();
}

/**
 * Find the task with the given ID.
 *
 * @param id The unique identifier of the task to look up.
 * @return A pointer to the Task object if found, or nullptr if the ID is invalid or out of range.
 */
const Task* TaskGraph::FindTask(TaskID id) const noexcept {
  const TaskNode* node = FindNode(id);

  if (node == nullptr) {
    return nullptr;
  }

  return node->task.get();
}

/**
 * Create a new task with the given name and action, add it to the task graph, and return its assigned ID
 *
 * @param name The name of the new task
 * @param action The action to be executed by the new task
 * @return The unique ID assigned to the newly added task
 */
TaskID TaskGraph::AddTask(std::string name, TaskAction action) {
  if (next_id_ == std::numeric_limits<TaskID>::max()) {
    throw std::overflow_error("Task ID space exhausted");
  }

  const TaskID assigned_id = next_id_;

  TaskNode node;
  node.task = std::make_unique<Task>(assigned_id, std::move(name), std::move(action));

  nodes_.push_back(std::move(node));

  ++ next_id_;

  return assigned_id;
}

/**
 * Check if the given task is ready to execute based on its state and dependency completion.
 *
 * @param node The task node to check.
 * @return true if the task is pending and all its dependencies are finished; false otherwise.
 * @throws std::logic_error if a dependency ID in the node's dependency list is invalid (cannot be found).
 */
bool TaskGraph::IsTaskReady(const TaskNode& node) const {
  if (node.task->GetTaskState() != TaskState::Pending) {
    return false;
  }

  for (TaskID dependency_id : node.dependencies) {
    const Task* dependency = FindTask(dependency_id);

    if (dependency == nullptr) {
      throw std::logic_error("TaskGraph contains an invalid dependency ID");
    }

    if (dependency->GetTaskState() != TaskState::Finished) {
      return false;
    }
  }

  return true;
}

/**
 * Returns a vector of raw pointers to tasks that are currently ready to execute.
 *
 * @return A vector of Task* pointers, one for each task whose dependencies have been satisfied.
 */
std::vector<Task*> TaskGraph::GetReadyTasks() {
  std::vector<Task*> ready_tasks;
  ready_tasks.reserve(nodes_.size());

  for (TaskNode& node : nodes_) {
    if (IsTaskReady(node)) {
      ready_tasks.push_back(node.task.get());
    }
  }

  return ready_tasks;
}

/**
 * Get all tasks whose dependencies are satisfied and that are ready to execute.
 *
 * @return A vector of const pointers to the ready tasks in the graph.
 */
std::vector<const Task*> TaskGraph::GetReadyTasks() const {
  std::vector<const Task*> ready_tasks;
  ready_tasks.reserve(nodes_.size());

  for (const TaskNode& node : nodes_) {
    if (IsTaskReady(node)) {
      ready_tasks.push_back(node.task.get());
    }
  }

  return ready_tasks;
}

/**
 * Check if a path exists between two task nodes in the graph.
 *
 * @param from The ID of the starting task.
 * @param to The ID of the target task.
 * @return true if a path exists from `from` to `to`; false otherwise.
 */
bool TaskGraph::HasPath(TaskID from, TaskID to) const {
  if (FindNode(from) == nullptr || FindNode(to) == nullptr) {
      return false;
  }

  std::vector<bool> visited(nodes_.size(), false);

  return HasPathImpl(from, to, visited);
}

/**
 * Check if there is a path from the current task to the target task in the task graph.
 *
 * @param current The starting task ID for the path search.
 * @param target The target task ID to reach.
 * @param visited A reference to a vector tracking visited tasks to avoid cycles.
 * @return true if a path exists from current to target, false otherwise.
 */
bool TaskGraph::HasPathImpl(TaskID current, TaskID target, std::vector<bool>& visited) const {
  if (current == target) {
    return true;
  }

  const TaskNode* node = FindNode(current);
  if (node == nullptr) {
    return false;
  }

  const auto index = static_cast<std::size_t>(current - 1);

  if (visited[index]) {
    return false;
  }

  visited[index] = true;

  for (TaskID dependent_id : node->dependents) {
    if (HasPathImpl(dependent_id, target, visited)) {
      return true;
    }
  }

  return false;
}

/// Add the edge
///   dependency -> dependent
/// For example, AddDependency(compile, link) means that Link depends on Compile.
void TaskGraph::AddDependency(TaskID dependency, TaskID dependent) {
  // Check for invalid ID
  TaskNode* dependency_node = FindNode(dependency);
  if (dependency_node == nullptr) {
    throw std::out_of_range("Dependency task does not exist");
  }

  // Check for invalid ID
  TaskNode* dependent_node = FindNode(dependent);
  if (dependent_node == nullptr) {
    throw std::out_of_range("Dependency task does not exist");
  }

  // Check for self-dependency
  if (dependency_node == dependent_node) {
    throw std::invalid_argument("Task cannot depend on itself");
  }

  // Check if an edge already exists
  const auto it = std::find(
    dependent_node->dependencies.begin(),
    dependent_node->dependencies.end(),
    dependency);

  if (it != dependent_node->dependencies.end()) {
    throw std::logic_error("Dependency already exists");
  }

  // Check if a cycle will be formed
  if (HasPath(dependent, dependency)) {
    throw std::logic_error("Adding this edge will form a cycle");
  }

  dependent_node->dependencies.push_back(dependency);

  // Roll back if the second write fails
  try {
    dependency_node->dependents.push_back(dependent);
  } catch (...) {
    dependent_node->dependencies.pop_back();
    throw;
  }
}

} // namespace minibuild
