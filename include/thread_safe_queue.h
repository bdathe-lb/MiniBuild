#ifndef MINI_BUILD_THREAD_SAFE_QUEUE_H
#define MINI_BUILD_THREAD_SAFE_QUEUE_H

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>

namespace minibuild {

template <typename T>
class ThreadSafeQueue {
private:
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::queue<T> queue_;
  bool closed_{false};

public:
  ThreadSafeQueue() = default;

  ThreadSafeQueue(const ThreadSafeQueue&) = delete;
  ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

  ThreadSafeQueue(ThreadSafeQueue&&) = delete;
  ThreadSafeQueue& operator=(ThreadSafeQueue&&) = delete;

  [[nodiscard]]
  bool Push(T value);

  [[nodiscard]]
  bool TryPop(T& value);

  [[nodiscard]]
  bool WaitAndPop(T& value);

  void Close();

  [[nodiscard]]
  bool IsClosed() const;

  [[nodiscard]]
  bool Empty() const;

  [[nodiscard]]
  std::size_t Size() const;
};

template<typename T>
bool ThreadSafeQueue<T>::IsClosed() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return closed_;
}

template<typename T>
bool ThreadSafeQueue<T>::Empty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.empty();
}

template<typename T>
std::size_t ThreadSafeQueue<T>::Size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size();  
}

template<typename T>
bool ThreadSafeQueue<T>::Push(T value) {
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (closed_) {
      return false;
    }

    queue_.push(std::move(value));
  }

  condition_.notify_one();
  return true;
}

template<typename T>
bool ThreadSafeQueue<T>::TryPop(T& value) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (queue_.empty()) {
    return false;
  }

  value = std::move(queue_.front());
  queue_.pop();

  return true;
}

template<typename T>
bool ThreadSafeQueue<T>::WaitAndPop(T& value) {
  std::unique_lock<std::mutex> lock(mutex_);

  condition_.wait(lock, [this] {
    return closed_ || !queue_.empty();
  });

  if (queue_.empty()) {
    return false;
  }
  
  value = std::move(queue_.front());
  queue_.pop();

  return true;
}

template<typename T>
void ThreadSafeQueue<T>::Close() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return;
    }

    closed_ = true;
  }
  
  condition_.notify_all();
}

} // minibuild

#endif // !MINI_BUILD_THREAD_SAFE_QUEUE_H
