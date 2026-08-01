#include "test_framework.h"
#include "thread_safe_queue.h"

#include <algorithm>
#include <cstddef>
#include <future>
#include <numeric>
#include <thread>
#include <vector>
#include <atomic>

namespace  {

using minibuild::ThreadSafeQueue;
using namespace std::chrono_literals;

static_assert(std::is_default_constructible_v<ThreadSafeQueue<int>>);

static_assert(!std::is_copy_constructible_v<ThreadSafeQueue<int>>);
static_assert(!std::is_copy_assignable_v<ThreadSafeQueue<int>>);

static_assert(!std::is_move_constructible_v<ThreadSafeQueue<int>>);
static_assert(!std::is_move_assignable_v<ThreadSafeQueue<int>>);

TEST_CASE("ThreadSafeQueue: init state") {
  ThreadSafeQueue<int> queue;

  CHECK(queue.Size() == 0);
  CHECK(queue.Empty() == true);
  CHECK(queue.IsClosed() == false);
}

TEST_CASE("ThreadSafeQueue: `Push` succeeded and status updated") {
  ThreadSafeQueue<int> queue;

  CHECK(queue.Push(10));
  CHECK(queue.Size() == 1);
  CHECK(!queue.Empty());
  CHECK(!queue.IsClosed());

  CHECK(queue.Push(20));
  CHECK(queue.Size() == 2);
}

TEST_CASE("ThreadSafeQueue: `TryPop` maintains FIFO") {
  ThreadSafeQueue<int> queue;

  int value = 0;

  static_cast<void>(queue.Push(10));
  static_cast<void>(queue.Push(20));
  static_cast<void>(queue.Push(30));

  CHECK(queue.TryPop(value));
  CHECK(value == 10);

  CHECK(queue.TryPop(value));
  CHECK(value == 20);

  CHECK(queue.TryPop(value));
  CHECK(value == 30);

  CHECK(queue.Empty());
  CHECK(queue.Size() == 0);
}

TEST_CASE("ThreadSafeQueue: `TryPop` on an empty queue fails immediately") {
  ThreadSafeQueue<int> queue;

  int value = 123;

  CHECK(!queue.TryPop(value));
  CHECK(value == 123);
}

TEST_CASE("ThreadSafeQueue: supports move-only types.") {
  ThreadSafeQueue<std::unique_ptr<int>> queue;
  std::unique_ptr<int> value;

  CHECK(queue.Push(std::make_unique<int>(42)));
  CHECK(queue.TryPop(value));
  CHECK(value != nullptr);
  CHECK(*value == 42);
}

TEST_CASE("ThreadSafeQueue: `Close` changes the closed state") {
  ThreadSafeQueue<int> queue;

  CHECK(!queue.IsClosed());

  queue.Close();

  CHECK(queue.IsClosed());
  CHECK(queue.Empty());
}

TEST_CASE("ThreadSafeQueue: `Close` can be called repeatedly") {
  ThreadSafeQueue<int> queue;

  CHECK(!queue.IsClosed());

  queue.Close();
  queue.Close();
  queue.Close();

  CHECK(queue.IsClosed());
  CHECK(queue.Empty());
}

TEST_CASE("ThreadSafeQueue: `Push` is rejected after Close") {
  ThreadSafeQueue<int> queue;

  queue.Close();

  CHECK(!queue.Push(10));
  CHECK(queue.Empty());
  CHECK(queue.Size() == 0);
}

TEST_CASE("ThreadSafeQueue: after closing, you can still drain/take all existing elements") {
  ThreadSafeQueue<int> queue;

  CHECK(queue.Push(10));
  CHECK(queue.Push(20));

  queue.Close();

  int value;
  CHECK(queue.TryPop(value));
  CHECK(value == 10);

  CHECK(queue.TryPop(value));
  CHECK(value == 20);

  CHECK(!queue.TryPop(value));
  CHECK(queue.Empty());
  CHECK(queue.IsClosed());
}

TEST_CASE("ThreadSafeQueue: `WaitAndPop` returns false when closed and empty") {
  ThreadSafeQueue<int> queue;
  queue.Close();

  int value = 0;

  CHECK(!queue.WaitAndPop(value));
}

TEST_CASE("ThreadSafeQueue: when closed but not empty, WaitAndPop continues to pop elements") {
  ThreadSafeQueue<int> queue;
  CHECK(queue.Push(10));
  CHECK(queue.Push(20));
  queue.Close();

  int value = 0;

  CHECK(queue.WaitAndPop(value));
  CHECK(value == 10);

  CHECK(queue.WaitAndPop(value));
  CHECK(value == 20);

  CHECK(!queue.WaitAndPop(value));
  CHECK(queue.Empty());
  CHECK(queue.IsClosed());
}

TEST_CASE("ThreadSafeQueue: test that WaitAndPop actually blocks") {
  ThreadSafeQueue<int> queue;

  struct PopResult {
    bool success;
    int value;
  };

  std::promise<PopResult> result_promise;
  std::future<PopResult> result_future =
      result_promise.get_future();

  std::thread consumer([&] {
    int value = -1;
    const bool success = queue.WaitAndPop(value);

    result_promise.set_value(PopResult{success, value});
  });

  const auto status_before_push = result_future.wait_for(100ms);
  const bool push_succeeded = queue.Push(42);

  consumer.join();

  const PopResult result =
      result_future.get();

  CHECK(status_before_push == std::future_status::timeout);

  CHECK(push_succeeded);
  CHECK(result.success);
  CHECK(result.value == 42);
}

TEST_CASE("ThreadSafeQueue: closing an empty queue unblocks WaitAndPop") {
  ThreadSafeQueue<int> queue;

  struct PopResult {
    bool success;
    int value;
  };

  std::promise<PopResult> result_promise;
  std::future<PopResult> result_future =
      result_promise.get_future();

  std::thread consumer([&] {
    int value = -1;
    const bool success = queue.WaitAndPop(value);

    result_promise.set_value(PopResult{success, value});
  });

  const auto status_before_push = result_future.wait_for(100ms);

  queue.Close();

  consumer.join();

  const PopResult result =
      result_future.get();

  CHECK(status_before_push == std::future_status::timeout);

  CHECK(!result.success);
  CHECK(result.value == -1);
}

TEST_CASE("ThreadSafeQueue: Close wakes all waiting consumers") {
  constexpr std::size_t consumer_count = 4;

  ThreadSafeQueue<int> queue;

  std::vector<std::promise<void>> started_promises(consumer_count);
  std::vector<std::future<void>> started_futures;
  started_futures.reserve(consumer_count);

  std::vector<std::promise<bool>> result_promises(consumer_count);
  std::vector<std::future<bool>> result_futures;
  result_futures.reserve(consumer_count);

  for (std::size_t i = 0; i < consumer_count; ++ i) {
    started_futures.push_back(
      started_promises[i].get_future()
    );

    result_futures.push_back(
      result_promises[i].get_future()
    );
  }

  std::vector<std::thread> consumers;
  consumers.reserve(consumer_count);

  for (std::size_t i = 0; i < consumer_count; ++ i) {
    consumers.emplace_back([&, i] {
      started_promises[i].set_value();

      int value = -1;
      const bool success = queue.WaitAndPop(value);

      result_promises[i].set_value(success);
    });
  }

  for (auto& started_future : started_futures) {
    started_future.wait();
  }

  std::this_thread::sleep_for(100ms);

  bool all_blocked_before_close = true;

  for (auto& result_future : result_futures) {
    const bool still_blocked =
      result_future.wait_for(0ms) ==
      std::future_status::timeout;

    all_blocked_before_close &=
      still_blocked;
  }

  queue.Close();

  bool all_woke_after_close = true;

  for (auto& result_future : result_futures) {
    const bool became_ready =
        result_future.wait_for(1s) ==
        std::future_status::ready;

    all_woke_after_close &=
        became_ready;
  }

for (auto& consumer : consumers) {
    consumer.join();
  }

  bool all_returned_false = true;

  for (auto& result_future : result_futures) {
    const bool success =
        result_future.get();

    all_returned_false &= !success;
  }

  CHECK(all_blocked_before_close);
  CHECK(all_woke_after_close);
  CHECK(all_returned_false);
}

TEST_CASE("ThreadSafeQueue: multiple producers do not lose elements") {
  constexpr std::size_t producer_count = 4;
  constexpr int items_per_producer = 1000;
  constexpr int total_items =
      static_cast<int>(producer_count) *
      items_per_producer;

  ThreadSafeQueue<int> queue;

  std::atomic<bool> push_failed{false};

  std::vector<std::thread> producers;
  producers.reserve(producer_count);

  for (std::size_t i = 0; i < producer_count; ++i) {
    producers.emplace_back([&, i] {
      const int begin =
          static_cast<int>(i) *
          items_per_producer;

      const int end =
          begin + items_per_producer;

      for (int value = begin; value < end; ++value) {
        if (!queue.Push(value)) {
          push_failed.store(true);
        }
      }
    });
  }

  for (std::thread& producer : producers) {
    producer.join();
  }

  CHECK(!push_failed.load());
  CHECK(
      queue.Size() ==
      static_cast<std::size_t>(total_items));

  std::vector<int> actual_values;
  actual_values.reserve(
      static_cast<std::size_t>(total_items));

  int value = -1;

  while (queue.TryPop(value)) {
    actual_values.push_back(value);
  }

  std::sort(
      actual_values.begin(),
      actual_values.end());

  std::vector<int> expected_values(
      static_cast<std::size_t>(total_items));

  std::iota(
      expected_values.begin(),
      expected_values.end(),
      0);

  CHECK(
      actual_values.size() ==
      static_cast<std::size_t>(total_items));

  CHECK(actual_values == expected_values);
  CHECK(queue.Empty());
  CHECK(queue.Size() == 0);
}

TEST_CASE("ThreadSafeQueue: multiple consumers consume each value once") {
  constexpr std::size_t consumer_count = 4;
  constexpr int item_count = 4000;

  ThreadSafeQueue<int> queue;

  for (int value = 0; value < item_count; ++value) {
    CHECK(queue.Push(value));
  }

  queue.Close();

  std::vector<std::vector<int>> consumed_by_thread(consumer_count);

  std::vector<std::thread> consumers;
  consumers.reserve(consumer_count);

  for (std::size_t i = 0; i < consumer_count; ++i) {
    consumers.emplace_back([&, i] {
      int value = 0;

      while (queue.WaitAndPop(value)) {
        consumed_by_thread[i].push_back(value);
      }
    });
  }

  for (std::thread& consumer : consumers) {
    consumer.join();
  }

  std::vector<int> consumed_values;
  consumed_values.reserve(static_cast<std::size_t>(item_count));

  for (const auto& local_values : consumed_by_thread) {
    consumed_values.insert(
        consumed_values.end(),
        local_values.begin(),
        local_values.end());
  }

  std::sort(consumed_values.begin(),
      consumed_values.end());

  std::vector<int> expected_values(
      static_cast<std::size_t>(item_count));

  std::iota( expected_values.begin(),
      expected_values.end(),
      0);

  CHECK(consumed_values == expected_values);
  CHECK(queue.Empty());
  CHECK(queue.IsClosed());
}

TEST_CASE("ThreadSafeQueue: concurrent producers and consumers") {
  constexpr std::size_t producer_count = 4;
  constexpr std::size_t consumer_count = 4;
  constexpr int items_per_producer = 1000;

  constexpr int total_items = static_cast<int>(producer_count) * items_per_producer;

  ThreadSafeQueue<int> queue;

  std::promise<void> start_promise;
  std::shared_future<void> start_future = start_promise.get_future().share();

  std::vector<std::vector<int>> consumed_by_thread(
      consumer_count);

  for (auto& values : consumed_by_thread) {
    values.reserve(
        static_cast<std::size_t>(total_items) /
        consumer_count);
  }

  std::atomic<bool> push_failed{false};

  std::vector<std::thread> consumers;
  consumers.reserve(consumer_count);

  for (std::size_t i = 0; i < consumer_count; ++ i) {
    consumers.emplace_back([&, i, start_future] {
      start_future.wait();

      int value = 0;

      while (queue.WaitAndPop(value)) {
        consumed_by_thread[i].push_back(value);
      }
    });
  }

  std::vector<std::thread> producers;
  producers.reserve(producer_count);

  for (std::size_t i = 0; i < producer_count; ++i) {
    producers.emplace_back([&, i, start_future] {
      start_future.wait();

      const int begin =
          static_cast<int>(i) * items_per_producer;

      const int end =
          begin + items_per_producer;

      for (int value = begin; value < end; ++value) {
        if (!queue.Push(value)) {
          push_failed.store(
              true,
              std::memory_order_relaxed);
        }
      }
    });
  }

  start_promise.set_value();

  for (std::thread& producer : producers) {
    producer.join();
  }

  queue.Close();

  for (std::thread& consumer : consumers) {
    consumer.join();
  }

  std::vector<int> consumed_values;
  consumed_values.reserve(
      static_cast<std::size_t>(total_items));

  for (const auto& local_values : consumed_by_thread) {
    consumed_values.insert(
        consumed_values.end(),
        local_values.begin(),
        local_values.end());
  }

  std::sort(
      consumed_values.begin(),
      consumed_values.end());

  std::vector<int> expected_values(
      static_cast<std::size_t>(total_items));

  std::iota(
      expected_values.begin(),
      expected_values.end(),
      0);

  CHECK(!push_failed.load(
      std::memory_order_relaxed));

  CHECK(
      consumed_values.size() ==
      static_cast<std::size_t>(total_items));

  CHECK(consumed_values == expected_values);

  CHECK(queue.Empty());
  CHECK(queue.IsClosed());
}

} // namespace
