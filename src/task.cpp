#include "task.h"

#include <stdexcept>
#include <utility>

namespace minibuild {

/**
 * Constructs a Task with the specified identifier, name, and action.
 *
 * @param id The identifier assigned to the task.
 * @param name The task's human-readable name.
 * @param action The function to execute when the task runs.
 */
Task::Task(TaskID id, std::string name, TaskAction action)
  : id_(id)
  , name_(std::move(name))
  , action_(std::move(action)) {

  // NOTE:
  // Since the parameters are already passed by value, they can be moved into member variables.

  // NOTE:
  // Any successfully constructed Task must have a valid name and an executable task body.
  if (name_.empty()) {
    throw std::invalid_argument("Task name cannot be empty");
  }

  if (!action_) {
    throw std::invalid_argument("Task action cannot be empty");
  }
}

/**
 * Get the task ID.
 *
 * @return The task ID.
 */
TaskID Task::GetTaskID() const noexcept {
  return id_;
}

/**
 * Gets the current task state.
 *
 * @return The current task state.
 */
TaskState Task::GetTaskState() const noexcept {
  return state_.load();
}

/**
 * Returns the name of the task.
 *
 * @return A const reference to the task name.
 */
const std::string& Task::GetTaskName() const noexcept {
  return name_;
}

/**
 * Executes the task action, updating state to Finished on success, or to Failed and rethrowing the exception on failure.
 */
void Task::Execute() {
  // NOTE:
  // Maintain the state machine's state to prevent redundant execution
  // Pending -> Running -> Finished/Failed

  TaskState expected = TaskState::Pending;
  const bool success = state_.compare_exchange_strong(expected, TaskState::Running);

  if (!success) {
     throw std::logic_error("Task can only be executed from Pending state");
  }

  try {
    action_();
    state_.store(TaskState::Finished);
  } catch (...) {
    state_.store(TaskState::Failed);
    throw;
  }
}

} // namespace minibuild

