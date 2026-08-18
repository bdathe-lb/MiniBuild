#include "thread_pool.h"

#include <cstddef>
#include <mutex>
#include <stdexcept>

namespace minibuild {

/**
 * Creates a thread pool with the specified number of worker threads. If the worker count is zero, throws an invalid_argument exception. Each worker thread is launched to run the WorkerLoop function. If thread creation fails, the task queue is closed and all worker threads are joined before rethrowing the exception.
 */
ThreadPool::ThreadPool(std::size_t worker_count) {
  if (worker_count == 0) {
    throw std::invalid_argument("The number of worker threads cannot be 0");
  }

  workers_.reserve(worker_count);

  try {
    for (std::size_t i = 0; i < worker_count; ++ i) {
      workers_.emplace_back([this] {
        WorkerLoop();
      });
    }
  } catch (...) {
    task_queue_.Close();

    for (auto& worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }

    completed_queue_.Close();

    throw;
  }
}

/**
 * Destroys the thread pool and stops all worker threads.
 */
ThreadPool::~ThreadPool() {
  Stop();
}

/**
 * Submits a task to the thread pool for execution
 *
 * @param task The task to be submitted for execution
 * @return true if the task was successfully enqueued, false otherwise
 */
bool ThreadPool::Submit(Task* task) {
  if (!task) {
    throw std::invalid_argument("Task cannot be null");
  }

  return task_queue_.Push(task);
}

/**
 * Waits for and removes a completed task from the completed queue if available.
 *
 * @param task Pointer to the task object to be popped from the completed queue.
 * @return True if a completed task was found and popped; false otherwise.
 */
bool ThreadPool::WaitAndPopCompleted(Task*& task) {
  return completed_queue_.WaitAndPop(task);
}

/**
 * Returns the number of worker threads in the thread pool.
 *
 * @return the size of the workers vector, representing the number of active worker threads.
 */
std::size_t ThreadPool::WorkerCount() const noexcept {
  return workers_.size();
}

/**
 * Returns true if the thread pool has been stopped and no more tasks can be submitted.
 *
 * @return true if the thread pool is stopped, false otherwise.
 */
bool ThreadPool::IsStopped() const {
  return task_queue_.IsClosed();
}

/**
 * Runs the worker thread loop, continuously retrieving and executing tasks from the task queue.
 *
 * @return void
 * @note The worker remains active until the task queue is empty. Tasks are processed one at a time, and any exceptions during execution are caught but do not terminate the worker.
 */
void ThreadPool::WorkerLoop() {
  Task* task = nullptr;

  // Wait for Task
  while (task_queue_.WaitAndPop(task)) {
    try {
      task->Execute();
    } catch (...) {
      // Task::Execute() has already marked the task as Failed.
      // Keep the worker alive and continue processing tasks.
    }

    const bool pushed = completed_queue_.Push(task);
    if (!pushed) {
      break;
    }
  }
}

/**
 * Stops the thread pool by closing the task queue and joining all worker threads.
 *
 * @return void
 */
void ThreadPool::Stop() {
  std::call_once(stop_once_, [this] {
    task_queue_.Close();

    for (auto& worker : workers_) {
      if (worker.joinable())
        worker.join();
    }

    // All workers have disappeared;  
    // no new completion can be generated.
    completed_queue_.Close();
  });
}
} // namespace minibuild
