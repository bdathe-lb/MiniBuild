#ifndef MINI_BUILD_TASK_H
#define MINI_BUILD_TASK_H

#include <functional>
#include <ostream>
#include <cstdint>
#include <string>

namespace minibuild {

enum class TaskState {
  Pending,
  Running,
  Finished,
  Failed,
};

constexpr const char * TaskStateToString(TaskState state) {
  switch (state) {
    case TaskState::Pending:  return "Pending";
    case TaskState::Running:  return "Running";
    case TaskState::Finished: return "Finished";
    case TaskState::Failed:   return "Failed";
    default:                  return "Unknown";
  }
}

using TaskID = std::uint64_t;
using TaskAction = std::function<void()>;

class Task {
private:
  /// Identity information
  TaskID id_;
  std::string name_;

  /// Lifecycles
  TaskState state_{TaskState::Pending};

  /// Task executes the specific task
  TaskAction action_;

public:
  Task(TaskID id, std::string name, TaskAction action);

  // Prohibit default constructor
  Task() = delete;

  // Prohibit copying
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;

  // Prohibit moving
  Task(Task&&) = delete;
  Task& operator=(Task&&) = delete;

  [[nodiscard]] 
  TaskID GetTaskID() const noexcept;

  [[nodiscard]] 
  TaskState GetTaskState() const noexcept;

  [[nodiscard]] 
  const std::string& GetTaskName() const noexcept;

  void Execute();
};

inline std::ostream& operator<<(std::ostream& os, const Task& task) {
  os << "Task [" << task.GetTaskID() << "] {" << task.GetTaskName()
     << "} - State: " << TaskStateToString(task.GetTaskState());
  return os;
}

} // namespace minibuild

#endif // !MINI_BUILD_TASK_H
