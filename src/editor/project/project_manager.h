#ifndef PROJECT_MANAGER_H
#define PROJECT_MANAGER_H

#include <filesystem>
#include <string>
#include "project_assets.h"
#include "project_observer.h"

struct Project {
  std::filesystem::path path_;
  struct Config {
    std::string name;  // defaults to the folder name
    std::string author;
    std::string tags;
    std::string version;
    // Scene opened with the project, relative to the project root
    std::string startup_scene;
  } config;
};

class ProjectManager {
public:
  ProjectManager();

  // Poll for file system events
  void PollEvents();

  // Load project from specified directory, and open its startup scene
  bool Load(const std::filesystem::path& directory);

  // Write the project configuration
  bool Save() const;

  // Accessors
  const Project& GetProject() const;
  ProjectObserver& GetObserver();
  ProjectAssets& Assets();

  // Resolve path relative to project root
  std::filesystem::path AbsolutePath(const std::filesystem::path& path);
  // Path relative to the project root (relative paths are returned as is)
  std::filesystem::path RelativePath(const std::filesystem::path& path) const;

  const Project::Config& Config() const { return project_.config; }

  std::string ProjectName() const;

  // Total size of files under the project root (computed once, cached)
  uint64_t SizeOnDisk();
  void InvalidateSize() { size_cached_ = false; }

  /* ---------- scenes ---------- */
  // A project contains many scenes; one is open at a time.
  // Scene paths are relative to the project root
  void NewScene();
  bool OpenScene(const std::filesystem::path& path);
  // False when the scene has no path yet
  bool SaveScene();
  bool SaveSceneAs(const std::filesystem::path& path);

  void MarkSceneDirty() { scene_dirty_ = true; }
  bool IsSceneDirty() const { return scene_dirty_; }
  const std::filesystem::path& ScenePath() const { return scene_path_; }
  // Name of the open scene, "Untitled" until it is saved
  std::string SceneName() const;
  // Where a scene with the given name is saved (scenes/<name>.scene)
  std::filesystem::path ScenePathFromName(const std::string& name) const;

private:
  // Ensure project configuration exists
  bool EnsureConfig();

  // Open the startup scene. A project without scenes gets a saved
  // SampleScene, which becomes its startup scene
  void OpenStartupScene();

  Project project_;
  ProjectObserver observer_;
  ProjectAssets assets_;

  uint64_t size_bytes_ = 0;
  bool size_cached_ = false;

  // Open scene, empty if it was never saved
  std::filesystem::path scene_path_;
  bool scene_dirty_ = false;
};

#endif  // PROJECT_MANAGER_H
