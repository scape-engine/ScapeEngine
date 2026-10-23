#include "activity.h"

#include <mutex>

namespace Diagnostics {

namespace {
std::mutex activity_mutex;
std::string activity;
}  // namespace

void SetActivity(const std::string& text) {
  std::lock_guard<std::mutex> lock(activity_mutex);
  activity = text;
}

void ClearActivity() {
  std::lock_guard<std::mutex> lock(activity_mutex);
  activity.clear();
}

std::string GetActivity() {
  std::lock_guard<std::mutex> lock(activity_mutex);
  return activity;
}

}  // namespace Diagnostics