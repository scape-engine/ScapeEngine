#ifndef PROJECT_MANAGER_H
#define PROJECT_MANAGER_H

#include <filesystem>
#include <string>
#include "project_assets.h"
#include "project_observer.h"

struct Project {
  std::filesystem::path path_;
  struct Config {
    std::string name = "Unnamed Project";
    std::string author;
    std::string tags;
    std::string version;
  } config;
};

class ProjectManager {
public:
  ProjectManager();

  // Poll for file system events
  void PollEvents();

  // Load project from specified directory
  bool Load(const std::filesystem::path& directory);

  // Accessors
  const Project& GetProject() const;
  ProjectObserver& GetObserver();
  ProjectAssets& Assets();

  // Resolve path relative to project root
  std::filesystem::path AbsolutePath(const std::filesystem::path& path);

  const Project::Config& Config() const { return project_.config; }

  std::string ProjectName() const;

  // Total size of files under the project root (computed once, cached)
  uint64_t SizeOnDisk();
  void InvalidateSize() { size_cached_ = false; }

private:
  // Ensure project configuration exists
  bool EnsureConfig();

  Project project_;
  ProjectObserver observer_;
  ProjectAssets assets_;

  uint64_t size_bytes_ = 0;
  bool size_cached_ = false;
};

#endif  // PROJECT_MANAGER_H