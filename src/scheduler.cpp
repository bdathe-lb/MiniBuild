#include "scheduler.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace minibuild {

Scheduler::Scheduler(TaskGraph& graph, std::size_t worker_count)
  : graph_(graph)
  , pool_(worker_count) { }

void Scheduler::SubmitNewReadyTasks() {
  std::vector<Task*> ready_tasks = graph_.GetReadyTasks();

  // For C++ 17
  // erase-remove idiom
  ready_tasks.erase(
    std::remove_if(
      ready_tasks.begin(),
      ready_tasks.end(),
      [this](Task* task) {
        return submitted_.find(task->GetTaskID()) != submitted_.end();
      }
    ),
   ready_tasks.end() 
  );

  for (auto task : ready_tasks) {
    if (!pool_.Submit(task)) {
      throw std::logic_error("Failed to submit task to running thread pool");
    }

    submitted_.insert(task->GetTaskID());
  }
}

bool Scheduler::Run() {
  if (has_run_) {
    throw std::logic_error("Scheduler can only run once");
  }

  has_run_ = true;
  
  const std::size_t total_tasks_count = graph_.Size();
  std::size_t completed_count = 0;

  bool build_failed = false;

  try {
    // Empty DAG, this is a build success case
    if (total_tasks_count == 0) {
      return true;
    }

    // Initial ready tasks
    SubmitNewReadyTasks();

    while (completed_count < total_tasks_count) {
      if (completed_count == submitted_.size()) {
        // No submitted task is still in flight
        if (build_failed) {
          pool_.Stop();
          return false;
        }
        
        throw std::logic_error("Scheduler error");
      }

      Task* completed = nullptr;

      if (!pool_.WaitAndPopCompleted(completed)) {
        throw std::logic_error("Completion channel closed unexpectedly");
      }

      ++ completed_count;

      if (completed->GetTaskState() == TaskState::Failed) {
        build_failed = true;
      }

      if (!build_failed) {
        SubmitNewReadyTasks();
      }
    }
  } catch (...) {
    pool_.Stop();
    throw;
  }

  pool_.Stop();

  return true;
}

} // namespace minibuild
