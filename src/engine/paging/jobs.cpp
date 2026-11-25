#include "jobs.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace Jobs {

static std::mutex s_mutex;
static std::condition_variable s_wake;
static std::deque<std::function<void()>> s_queue;
static std::vector<std::thread> s_workers;
static std::atomic<bool> s_stopping{false};

static void _Work() {
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock lock(s_mutex);
      s_wake.wait(lock, [] { return s_stopping || !s_queue.empty(); });
      if (s_stopping)
        return;
      job = std::move(s_queue.front());
      s_queue.pop_front();
    }
    job();
  }
}

size_t Workers() {
  return std::max(2u, std::thread::hardware_concurrency()) - 1;
}

void Queue(std::function<void()> job) {
  {
    std::lock_guard lock(s_mutex);
    if (s_stopping)
      return;  // dropped: its future reports a broken promise
    // Started with the first job, so a process without any starts none
    if (s_workers.empty())
      for (size_t i = 0; i < Workers(); ++i)
        s_workers.emplace_back(_Work);
    s_queue.push_back(std::move(job));
  }
  s_wake.notify_one();
}

void ForEach(size_t count, const std::function<void(size_t)>& each) {
  // A helper that starts after this returns finds it closed and leaves
  // `each` alone
  struct Shared {
    std::atomic<size_t> next{0};
    std::mutex mutex;
    std::condition_variable idle;
    size_t running = 0;
    bool closed = false;
  };
  const auto shared = std::make_shared<Shared>();
  const auto run = [&each, count](Shared& shared) {
    for (size_t at; (at = shared.next++) < count;)
      each(at);
  };
  for (size_t i = 1; i < std::min(count, Workers()); ++i)
    Queue([shared, run] {
      {
        std::lock_guard lock(shared->mutex);
        if (shared->closed)
          return;
        ++shared->running;
      }
      run(*shared);
      std::lock_guard lock(shared->mutex);
      if (--shared->running == 0)
        shared->idle.notify_all();
    });
  run(*shared);
  std::unique_lock lock(shared->mutex);
  shared->closed = true;
  shared->idle.wait(lock, [&] { return shared->running == 0; });
}

bool Stopping() {
  return s_stopping;
}

void Shutdown() {
  std::deque<std::function<void()>> dropped;
  {
    std::lock_guard lock(s_mutex);
    s_stopping = true;
    dropped.swap(s_queue);
  }
  s_wake.notify_all();
  for (std::thread& worker : s_workers)
    worker.join();
  s_workers.clear();
}

}  // namespace Jobs
