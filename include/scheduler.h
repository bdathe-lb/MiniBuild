#ifndef MINI_BUILD_SCHEDULER_H
#define MINI_BUILD_SCHEDULER_H

#include "task.h"
#include "task_graph.h"
#include "thread_pool.h"

#include <cstddef>
#include <unordered_set>

namespace minibuild {

class Scheduler {
private:
  TaskGraph& graph_;
  ThreadPool pool_;

  std::unordered_set<TaskID> submitted_;
  bool has_run_{false};

private:
  void SubmitNewReadyTasks();

public:
  /// Constructor
  Scheduler(TaskGraph& graph, std::size_t worker_count);

  /// Prohibit copy constructor
  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;

  /// Prohibit move constructor
  Scheduler(Scheduler&&) = delete;
  Scheduler& operator=(Scheduler&&) = delete;

  [[nodiscard]]
  bool Run();
};

} // namespace minibuild

#endif // !MINI_BUILD_SCHEDULER_H
