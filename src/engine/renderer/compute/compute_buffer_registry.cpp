#include "compute_buffer_registry.h"

#include "engine/core/logger.h"

namespace {
const bgfx::VertexLayout& Vec4Layout() {
  static bgfx::VertexLayout layout = [] {
    bgfx::VertexLayout l;
    l.begin()
        .add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float)
        .end();
    return l;
  }();
  return layout;
}
}  // namespace

ComputeBufferRegistry& ComputeBufferRegistry::Instance() {
  static ComputeBufferRegistry instance;
  return instance;
}

ComputeBufferId ComputeBufferRegistry::Create(uint32_t vec4_count) {
  bgfx::DynamicVertexBufferHandle handle = bgfx::createDynamicVertexBuffer(
      vec4_count > 0 ? vec4_count : 1, Vec4Layout(),
      BGFX_BUFFER_COMPUTE_READ | BGFX_BUFFER_COMPUTE_FORMAT_32X4 |
          BGFX_BUFFER_COMPUTE_TYPE_FLOAT | BGFX_BUFFER_ALLOW_RESIZE);
  if (!bgfx::isValid(handle)) {
    Logger::getInstance().Log(LogLevel::Error,
                              "ComputeBufferRegistry: buffer creation failed");
    return INVALID_COMPUTE_BUFFER;
  }

  std::scoped_lock lock(mutex_);
  const ComputeBufferId id = next_id_++;
  buffers_[id] = handle;
  return id;
}

void ComputeBufferRegistry::Update(ComputeBufferId id,
                                   const std::vector<glm::vec4>& data) {
  if (data.empty())
    return;
  bgfx::DynamicVertexBufferHandle handle = Get(id);
  if (!bgfx::isValid(handle))
    return;
  bgfx::update(handle, 0,
               bgfx::copy(data.data(),
                          uint32_t(data.size() * sizeof(glm::vec4))));
}

void ComputeBufferRegistry::Release(ComputeBufferId id) {
  std::scoped_lock lock(mutex_);
  if (auto it = buffers_.find(id); it != buffers_.end()) {
    bgfx::destroy(it->second);
    buffers_.erase(it);
  }
}

bgfx::DynamicVertexBufferHandle ComputeBufferRegistry::Get(
    ComputeBufferId id) const {
  std::scoped_lock lock(mutex_);
  if (auto it = buffers_.find(id); it != buffers_.end())
    return it->second;
  bgfx::DynamicVertexBufferHandle invalid = BGFX_INVALID_HANDLE;
  return invalid;
}

void ComputeBufferRegistry::Clear() {
  std::scoped_lock lock(mutex_);
  for (auto& [id, handle] : buffers_)
    bgfx::destroy(handle);
  buffers_.clear();
  next_id_ = 1;
}
