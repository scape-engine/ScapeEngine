#ifndef CELL_JOBS_H
#define CELL_JOBS_H

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/paging/jobs.h"

// What a paging client (a grid of cells loading around the viewers) shares
// with the others: which cells are generating on the workers, and the order
// cells start in. Which cells a grid wants, and what a finished cell becomes,
// stay the client's own
namespace Paging {

// One grid's cells generating on the workers, by cell key
template <class Result>
class CellJobs {
public:
  bool Contains(uint64_t key) const { return running_.count(key) > 0; }
  size_t Size() const { return running_.size(); }

  template <class Work>
  void Start(uint64_t key, Work&& work) {
    running_[key] = Jobs::Submit(std::forward<Work>(work));
  }

  // Hands each finished cell to `take(key, result)`, while `more()` (e.g.
  // the frame budget)
  template <class Take>
  void Finish(Take&& take, const std::function<bool()>& more = nullptr) {
    for (auto it = running_.begin(); it != running_.end();) {
      if (more && !more())
        return;
      if (it->second.wait_for(std::chrono::seconds(0)) !=
          std::future_status::ready) {
        ++it;
        continue;
      }
      const uint64_t key = it->first;
      Result result = it->second.get();
      it = running_.erase(it);
      take(key, std::move(result));
    }
  }

  // Forgets them: those running finish unread
  void Clear() { running_.clear(); }

private:
  std::unordered_map<uint64_t, std::future<Result>> running_;
};

// The cells to start this frame, of every grid of a client, soonest first
class StartQueue {
public:
  // `soon`: how soon the cell is seen (e.g. its distance from the viewers)
  void Add(float soon, std::function<void()> start) {
    starts_.push_back({soon, std::move(start)});
  }

  // Starts the soonest until `busy()`
  void Run(const std::function<bool()>& busy) {
    std::sort(starts_.begin(), starts_.end(),
              [](const Entry& a, const Entry& b) { return a.soon < b.soon; });
    for (Entry& entry : starts_) {
      if (busy())
        break;
      entry.start();
    }
    starts_.clear();
  }

private:
  struct Entry {
    float soon;
    std::function<void()> start;
  };
  std::vector<Entry> starts_;
};

}  // namespace Paging

#endif  // CELL_JOBS_H
