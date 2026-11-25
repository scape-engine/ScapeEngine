#include "activity.h"

#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>

namespace Diagnostics {

namespace {
std::mutex tasks_mutex;
std::vector<std::pair<TaskId, std::string>> tasks;  // oldest first
TaskId next_task = 1;
}  // namespace

TaskId BeginTask(const std::string& name) {
  std::lock_guard<std::mutex> lock(tasks_mutex);
  tasks.emplace_back(next_task, name);
  return next_task++;
}

void EndTask(TaskId id) {
  std::lock_guard<std::mutex> lock(tasks_mutex);
  tasks.erase(std::remove_if(tasks.begin(), tasks.end(),
                             [id](const auto& task) { return task.first == id; }),
              tasks.end());
}

std::string GetActivity() {
  std::lock_guard<std::mutex> lock(tasks_mutex);
  if (tasks.empty())
    return {};
  std::string text = tasks.front().second;
  if (tasks.size() > 1)
    text += " (+" + std::to_string(tasks.size() - 1) + ")";
  return text;
}

}  // namespace Diagnostics
