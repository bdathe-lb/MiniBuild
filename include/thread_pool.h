#ifndef MINI_BUILD_THREAD_POOL_H
#define MINI_BUILD_THREAD_POOL_H

#include "task.h"
#include "thread_safe_queue.h"

#include <cstddef>
#include <thread>
#include <vector>
#include <mutex>

namespace minibuild {

class ThreadPool {
private:
  ThreadSafeQueue<Task*> task_queue_;
  ThreadSafeQueue<Task*> completed_queue_;

  std::vector<std::thread> workers_;
  std::once_flag stop_once_;

private:
  void WorkerLoop();
  
public:
  /// Constructor
  explicit ThreadPool(std::size_t worker_count);

  /// Prohibit copy constructor
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  /// Prohibit move constructor
  ThreadPool(ThreadPool&&) = delete;
  ThreadPool& operator=(ThreadPool&&) = delete;
  
  /// Destructor
  ~ThreadPool();

  /// Submit a task to the thread pool
  [[nodiscard]]
  bool Submit(Task* task);

  /// Wait for task completion and enqueue it into the completion queue
  [[nodiscard]]
  bool WaitAndPopCompleted(Task*& task);

  /// Stop thread pool
  void Stop();

  /// Get the number of threads
  [[nodiscard]]
  std::size_t WorkerCount() const noexcept;

  /// Whether the thread pool has stopped
  bool IsStopped() const;
};

} /// namespace minibuild

#endif // !MINI_BUILD_THREAD_POOL_H
