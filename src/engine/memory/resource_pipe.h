#ifndef RESOURCE_PIPE_H
#define RESOURCE_PIPE_H

#include <cstdint>
#include <functional>
#include <optional>
#include <queue>

class Resource;
class ResourceManager;

enum class TaskFlags : uint32_t {
  None = 0,
  UseRenderThread = 1 << 0  // Execute task on render thread
};

constexpr TaskFlags operator|(TaskFlags a, TaskFlags b) {
  return static_cast<TaskFlags>(static_cast<uint32_t>(a) |
                                static_cast<uint32_t>(b));
}

constexpr bool operator&(TaskFlags a, TaskFlags b) {
  return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

// Helper macros for task creation
#define BIND_TASK(_class, _member) \
  ResourceTask(std::bind(&_class::_member, this))

#define BIND_TASK_WITH_FLAGS(_class, _member, _flags) \
  ResourceTask(std::bind(&_class::_member, this), _flags)

class ResourceTask {
public:
  using TaskFunc = std::function<bool()>;

  template <typename Func>
  ResourceTask(Func func, TaskFlags flags = TaskFlags::None)
      : func(func), flags(flags) {}

private:
  friend class ResourceManager;

  // Function to execute, returns success/failure
  TaskFunc func;

  // Task configuration flags
  TaskFlags flags;
};

using NextTask = std::optional<ResourceTask>;

class ResourcePipe {
public:
  ResourcePipe(ResourcePipe&& other) noexcept
      : owner_id_(other.owner_id_),
        tasks_(std::move(other.tasks_)),
        activity_(other.activity_) {
    other.owner_id_ = 0;
    other.activity_ = 0;
  }

  ResourcePipe& operator=(ResourcePipe&& other) noexcept {
    if (this != &other) {
      owner_id_ = other.owner_id_;
      tasks_ = std::move(other.tasks_);
      activity_ = other.activity_;
      other.owner_id_ = 0;
      other.activity_ = 0;
    }
    return *this;
  }

  // Delete copy operations
  ResourcePipe(const ResourcePipe&) = delete;
  ResourcePipe& operator=(const ResourcePipe&) = delete;

  // Add a task to the pipe
  ResourcePipe& operator>>(ResourceTask&& task) {
    tasks_.emplace(task);
    return *this;
  }

  // Get the next task
  NextTask Next() {
    if (tasks_.empty())
      return std::nullopt;
    NextTask task = std::move(tasks_.front());
    tasks_.pop();
    return task;
  }

  // Get resource ID
  uint32_t Owner() const { return owner_id_; }

private:
  friend class Resource;
  friend class ResourceManager;

  explicit ResourcePipe(uint32_t owner_id)
      : owner_id_(owner_id), tasks_(), activity_(0) {}

  // Resource ID that owns this pipe
  uint32_t owner_id_;

  // Task queue
  std::queue<ResourceTask> tasks_;

  // Diagnostics task shown while the pipe is queued or running
  uint64_t activity_;
};

#endif  // RESOURCE_PIPE_H