#include "resource_manager.h"

ResourceManager::ResourceManager()
    : id_counter_(0),
      resources_(),
      async_pipes_(),
      async_pipes_size_(0),
      processor_running_(false),
      processor_(),
      mtx_processor_(),
      processor_state_(),
      cv_next_pipe_(),
      cv_awaiting_context_(),
      context_next_(false),
      context_result_(false),
      context_task_(nullptr) {}

ResourceManager::~ResourceManager() {
  // Stop processor thread (set under the lock so the wake-up can't be lost)
  {
    std::lock_guard lock(mtx_processor_);
    processor_running_ = false;
  }
  if (processor_.joinable()) {
    cv_next_pipe_.notify_all();  // Wake up the processor to check running flag
    processor_.join();
  }

  // Clear all resources
  CleanupAll();
}

void ResourceManager::UpdateContext() {
  // Check if a task is waiting for the render thread
  if (!context_next_)
    return;

  // The processor is blocked in cv_awaiting_context_.wait(), which released
  // this mutex. Holding it here makes the handoff race-free.
  std::lock_guard lock(mtx_processor_);
  if (!context_next_)
    return;

  // Execute the context thread task
  context_result_ = context_task_ ? context_task_() : false;

  // Clearing the flag is the wake-up condition (and stops re-running the task)
  context_next_ = false;
  cv_awaiting_context_.notify_all();
}

bool ResourceManager::Execute(ResourcePipe&& pipe) {
  // Start async pipe processing if not running
  if (!processor_running_) {
    processor_running_ = true;
    processor_ = std::thread(&ResourceManager::AsyncPipeProcessor, this);
  }

  // Ensure owner resource exists
  ResourceRef<Resource> resource = GetResource(pipe.Owner());
  if (!resource) {
    Logger::getInstance().Log(
        LogLevel::Warning,
        "ResourceManager: Failed to queue pipe, owner resource ID " +
            std::to_string(pipe.Owner()) + " is invalid");
    return false;
  }

  // Queue the pipe
  auto pipe_handle = std::make_unique<ResourcePipe>(std::move(pipe));
  bool success = async_pipes_.try_enqueue(std::move(pipe_handle));

  // Update resource state
  if (success) {
    async_pipes_size_++;
    resource->resource_state_ = ResourceState::QUEUED;
    cv_next_pipe_.notify_all();
  } else {
    resource->resource_state_ = ResourceState::FAILED;
  }

  return success;
}

bool ResourceManager::ExecuteSync(ResourcePipe&& pipe) {
  // Async processor shouldn't be running for sync execution
  if (processor_running_) {
    Logger::getInstance().Log(LogLevel::Warning,
                              "ResourceManager: Cannot execute pipe "
                              "synchronously, async processor is running");
    return false;
  }

  // Ensure owner resource exists
  ResourceRef<Resource> resource = GetResource(pipe.Owner());
  if (!resource) {
    Logger::getInstance().Log(
        LogLevel::Warning,
        "ResourceManager: Failed to execute pipe, owner resource ID " +
            std::to_string(pipe.Owner()) + " is invalid");
    return false;
  }

  // Execute each pipe task synchronously
  resource->resource_state_ = ResourceState::LOADING;

  while (NextTask next_task = pipe.Next()) {
    ResourceTask task = *next_task;
    bool success = task.func();
    if (!success) {
      resource->resource_state_ = ResourceState::FAILED;
      return false;
    }
  }

  // All tasks executed successfully
  resource->resource_state_ = ResourceState::READY;
  return true;
}

void ResourceManager::Release(ResourceID id) {
  ResourceRef<Resource> resource = GetResource(id);
  if (!resource)
    return;

  // Call the resource's destroy method to clean up GPU resources
  resource->Destroy();
  resource->resource_state_ = ResourceState::EMPTY;
  resources_.erase(id);
}

void ResourceManager::CleanupAll() {
  // First, set all resources to EMPTY state
  for (auto& [id, resource] : resources_) {
    if (resource) {
      resource->Destroy();
      resource->resource_state_ = ResourceState::EMPTY;
    }
  }

  // Then clear the map
  resources_.clear();

  Logger::getInstance().Log(LogLevel::Debug,
                            "ResourceManager: All resources cleaned up");
}

void ResourceManager::AsyncPipeProcessor() {
  while (processor_running_) {
    std::unique_lock lock(mtx_processor_);

    // Try to get the next pipe
    std::unique_ptr<ResourcePipe> pipe;
    bool next_pipe = async_pipes_.try_dequeue(pipe);

    if (next_pipe) {
      async_pipes_size_--;

      ResourceRef<Resource> resource = GetResource(pipe->Owner());
      if (!resource) {
        Logger::getInstance().Log(
            LogLevel::Warning,
            "ResourceManager: Failed to process pipe, owner resource ID " +
                std::to_string(pipe->Owner()) + " is invalid");
        continue;
      }

      // Update processor state
      processor_state_.SetLoading(resource->GetName());

      // Execute each pipe task
      resource->resource_state_ = ResourceState::LOADING;

      while (NextTask next_task = pipe->Next()) {
        ResourceTask task = *next_task;
        bool success = false;

        // Execute task on render thread if needed
        if (task.flags & TaskFlags::UseRenderThread) {
          context_task_ = task.func;
          context_next_ = true;
          // Predicate guards against spurious and lost wake-ups
          cv_awaiting_context_.wait(lock,
                                    [this] { return !context_next_.load(); });
          context_task_ = nullptr;
          success = context_result_;
        } else {
          // Execute on this thread
          success = task.func();
        }

        if (!success) {
          resource->resource_state_ = ResourceState::FAILED;
          break;
        }
      }

      // Check if all tasks were successful
      if (resource->resource_state_ != ResourceState::FAILED) {
        resource->resource_state_ = ResourceState::READY;
      }
    } else {
      // No pipes to process, wait for notification
      processor_state_.SetSleeping();
      cv_next_pipe_.wait(lock, [this]() {
        return !processor_running_ || async_pipes_.size_approx() > 0;
      });
    }
  }
}