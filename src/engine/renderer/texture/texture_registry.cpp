#include "texture_registry.h"

#include <algorithm>
#include <atomic>
#include <thread>

#include "engine/content/cache/image_cache.h"
#include "engine/content/loaders/image_loader.h"
#include "engine/core/logger.h"

TextureRegistry& TextureRegistry::Instance() {
  static TextureRegistry instance;
  return instance;
}

TextureId TextureRegistry::Acquire(const std::string& key, TextureType type,
                                   uint64_t sampler_flags) {
  if (key.empty())
    return INVALID_TEXTURE;

  std::scoped_lock lock(mutex_);

  const std::string id_key = std::to_string(static_cast<int>(type)) + ":" + key;
  if (auto it = ids_.find(id_key); it != ids_.end())
    return it->second;

  auto texture = Texture::Create(Content::ImageCache::Instance().Get(key), type,
                                 sampler_flags);
  if (!texture) {
    // Remembered as missing, to avoid repeated upload attempts
    ids_[id_key] = INVALID_TEXTURE;
    return INVALID_TEXTURE;
  }

  const TextureId id = next_id_++;
  textures_[id] = std::move(texture);
  ids_[id_key] = id;

  Logger::getInstance().Log(
      LogLevel::Debug,
      "[TextureRegistry] Uploaded '" + key + "' with id " + std::to_string(id));
  return id;
}

std::vector<TextureRegistry::PendingTexture> TextureRegistry::Prepare(
    const std::vector<std::pair<std::string, TextureType>>& textures) {
  // Not uploaded yet, each once
  std::vector<PendingTexture> jobs;
  {
    std::scoped_lock lock(mutex_);
    for (const auto& [key, type] : textures) {
      if (key.empty())
        continue;
      std::string id_key = std::to_string(static_cast<int>(type)) + ":" + key;
      const bool queued = std::any_of(
          jobs.begin(), jobs.end(),
          [&](const PendingTexture& job) { return job.id_key == id_key; });
      if (queued || ids_.count(id_key))
        continue;
      PendingTexture job;
      job.key = key;
      job.id_key = std::move(id_key);
      job.type = type;
      jobs.push_back(std::move(job));
    }
  }
  if (jobs.empty())
    return jobs;

  // Images already cached (e.g. embedded ones) are read, the rest decoded
  auto& images = Content::ImageCache::Instance();
  std::vector<const Content::ImageData*> sources(jobs.size(), nullptr);
  for (size_t i = 0; i < jobs.size(); ++i) {
    jobs[i].cached = images.Contains(jobs[i].key);
    if (jobs[i].cached)
      sources[i] = &images.Get(jobs[i].key);
  }

  // Decode and build mips in parallel
  std::atomic<size_t> next{0};
  const auto work = [&] {
    for (size_t i = next++; i < jobs.size(); i = next++) {
      PendingTexture& job = jobs[i];
      if (!job.cached)
        job.decoded = Content::ImageLoader::LoadFile(job.key);
      job.upload =
          Texture::Prepare(job.cached ? *sources[i] : job.decoded, job.type);
    }
  };
  const size_t thread_count = std::min<size_t>(
      jobs.size(), std::max(1u, std::thread::hardware_concurrency()));
  std::vector<std::thread> threads;
  for (size_t i = 1; i < thread_count; ++i)
    threads.emplace_back(work);
  work();  // this thread helps too
  for (std::thread& thread : threads)
    thread.join();

  Logger::getInstance().Log(LogLevel::Debug,
                            "[TextureRegistry] Prepared " +
                                std::to_string(jobs.size()) + " textures on " +
                                std::to_string(thread_count) + " threads");
  return jobs;
}

void TextureRegistry::Upload(std::vector<PendingTexture> textures,
                             uint64_t sampler_flags) {
  auto& images = Content::ImageCache::Instance();
  for (PendingTexture& job : textures) {
    // Another model sharing the image may have uploaded it meanwhile
    {
      std::scoped_lock lock(mutex_);
      if (ids_.count(job.id_key))
        continue;
    }
    if (!job.cached)
      images.Put(job.key, std::move(job.decoded));

    auto texture = Texture::Create(job.upload, sampler_flags);
    std::scoped_lock lock(mutex_);
    if (!texture) {
      ids_[job.id_key] = INVALID_TEXTURE;  // remembered as missing
      continue;
    }
    const TextureId id = next_id_++;
    textures_[id] = std::move(texture);
    ids_[job.id_key] = id;
  }
}

bgfx::TextureHandle TextureRegistry::Get(TextureId id) const {
  std::scoped_lock lock(mutex_);

  auto it = textures_.find(id);
  if (it == textures_.end()) {
    bgfx::TextureHandle invalid = BGFX_INVALID_HANDLE;
    return invalid;
  }
  return it->second->Handle();
}

void TextureRegistry::Clear() {
  std::scoped_lock lock(mutex_);

  textures_.clear();  // shared pointers destroy the GPU handles
  ids_.clear();
  next_id_ = 1;
}
