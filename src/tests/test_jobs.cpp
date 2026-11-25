// Checks must run in release builds too
#undef NDEBUG
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "engine/paging/frame_budget.h"
#include "engine/paging/jobs.h"

namespace Tests {

// Jobs run and return their results; ForEach calls each index once, even from
// inside more jobs than there are workers (none waits for a job that can't
// start); queued jobs are dropped by Shutdown. The frame budget runs out
// within a Spend and starts again each frame
void TestJobs() {
  assert(Jobs::Submit([] { return 7; }).get() == 7);

  constexpr size_t count = 1000;
  std::vector<std::future<size_t>> jobs;
  for (size_t job = 0; job < Jobs::Workers() * 2; ++job)
    jobs.push_back(Jobs::Submit([] {
      std::vector<std::atomic<int>> calls(count);
      Jobs::ForEach(count, [&](size_t at) { ++calls[at]; });
      size_t once = 0;
      for (const auto& call : calls)
        once += call == 1;
      return once;
    }));
  for (auto& job : jobs)
    assert(job.get() == count);

  FrameBudget::SetTotal(1.0f);
  FrameBudget::Begin();
  {
    const FrameBudget::Spend spend;
    assert(FrameBudget::More());
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    assert(!FrameBudget::More());
  }
  assert(!FrameBudget::More());
  FrameBudget::Begin();
  assert(FrameBudget::More());
  FrameBudget::SetTotal(2.0f);

  Jobs::Shutdown();
  auto dropped = Jobs::Submit([] { return 1; });
  bool broken = false;
  try {
    dropped.get();
  } catch (const std::future_error&) {
    broken = true;
  }
  assert(broken && Jobs::Stopping());
  std::cout << "Jobs: OK" << std::endl;
}

}  // namespace Tests
