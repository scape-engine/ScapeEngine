#ifndef JOBS_H
#define JOBS_H

#include <functional>
#include <future>
#include <memory>
#include <type_traits>

// The engine's worker threads (the cores but one). Background work, such as
// generating a cell, is queued here so every system shares the same cores
// instead of starting threads of its own; jobs run oldest first
namespace Jobs {

void Queue(std::function<void()> job);

// Queues `work`; its future is ready once a worker has run it. Dropping the
// future neither waits for the job nor cancels it
template <class Work>
auto Submit(Work&& work) -> std::future<std::invoke_result_t<Work>> {
  using Result = std::invoke_result_t<Work>;
  auto task =
      std::make_shared<std::packaged_task<Result()>>(std::forward<Work>(work));
  std::future<Result> result = task->get_future();
  Queue([task] { (*task)(); });
  return result;
}

// Calls `each` for 0..count-1, on this thread and on idle workers; returns
// once every call has. Safe from inside a job: it never waits for a job that
// hasn't started
void ForEach(size_t count, const std::function<void(size_t)>& each);

// How many workers run jobs at once
size_t Workers();

// True once Shutdown has begun: long jobs check it to stop early
bool Stopping();

// Drops the queued jobs (their futures report a broken promise) and waits for
// the running ones. Before the systems their jobs use go
void Shutdown();

}  // namespace Jobs

#endif  // JOBS_H
