#ifndef RESOURCE_MANAGER_H
#define RESOURCE_MANAGER_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#include "engine/core/logger.h"
#include "engine/core/singleton.hpp"
#include "engine/resources/concurrent_queue.h"
#include "resource.h"
#include "resource_pipe.h"

template <typename T>
using ResourceRef = std::shared_ptr<T>;

class ResourceManager : public Singleton<ResourceManager> {
public:
  ResourceManager();
  ~ResourceManager();

  // Updates resource processing from render thread
  void UpdateContext();

  // Queue a resource pipe for async execution
  bool Execute(ResourcePipe&& pipe);

  // Execute a resource pipe synchronously (dependencies)
  bool ExecuteSync(ResourcePipe&& pipe);

  // Create a new resource
  template <typename T, typename... Args>
  std::pair<ResourceID, ResourceRef<T>> Create(const std::string& name,
                                               Args&&... args) {
    static_assert(std::is_base_of<Resource, T>::value,
                  "Type must inherit from Resource");

    ResourceID id = ++id_counter_;
    auto result = resources_.emplace(
        id, std::make_shared<T>(std::forward<Args>(args)...));
    auto& resource = result.first->second;
    resource->resource_id_ = id;
    resource->resource_name_ = name;
    return std::make_pair(id, std::static_pointer_cast<T>(resource));
  }

  // Add a resource built elsewhere (e.g. read on a worker thread)
  template <typename T>
  ResourceID Add(const std::string& name, ResourceRef<T> resource) {
    static_assert(std::is_base_of<Resource, T>::value,
                  "Type must inherit from Resource");

    ResourceID id = ++id_counter_;
    resource->resource_id_ = id;
    resource->resource_name_ = name;
    resources_.emplace(id, std::move(resource));
    return id;
  }

  // Get resource by ID
  ResourceRef<Resource> GetResource(ResourceID id) {
    auto it = resources_.find(id);
    return (it != resources_.end()) ? it->second : nullptr;
  }

  // Get resource by ID with type casting
  template <typename T>
  ResourceRef<T> GetResourceAs(ResourceID id) {
    static_assert(std::is_base_of<Resource, T>::value,
                  "Type must inherit from Resource");

    auto it = resources_.find(id);
    return (it != resources_.end()) ? std::static_pointer_cast<T>(it->second)
                                    : nullptr;
  }

  // Release a resource
  void Release(ResourceID id);

  // Clean up all resources - important for BGFX shutdown
  void CleanupAll();

  // Get number of queued tasks
  uint32_t QueuedTaskCount() const { return async_pipes_size_; }

  // Processor state
  struct ProcessorState {
    bool loading;
    std::string name;

    void SetSleeping() {
      loading = false;
      name = "";
    }
    void SetLoading(std::string name) {
      loading = true;
      this->name = std::move(name);
    }
  };

  // Get processor state
  const ProcessorState& GetProcessorState() const { return processor_state_; }

private:
  // Resource ID counter
  std::atomic<ResourceID> id_counter_;

  // Resource registry
  std::unordered_map<ResourceID, ResourceRef<Resource>> resources_;

  // Async processing
  ConcurrentQueue<std::unique_ptr<ResourcePipe>> async_pipes_;
  std::atomic<uint32_t> async_pipes_size_;

  // Process pending async pipes
  void AsyncPipeProcessor();

  // Thread management
  std::atomic<bool> processor_running_;
  std::thread processor_;
  std::mutex mtx_processor_;
  ProcessorState processor_state_;

  // Synchronization
  std::condition_variable cv_next_pipe_;
  std::condition_variable cv_awaiting_context_;

  // Context thread task handling
  std::atomic<bool> context_next_;
  std::atomic<bool> context_result_;
  ResourceTask::TaskFunc context_task_;
};

#endif  // RESOURCE_MANAGER_H