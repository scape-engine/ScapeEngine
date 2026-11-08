#ifndef PROJECT_OBSERVER_H
#define PROJECT_OBSERVER_H

#include <algorithm>
#include <efsw/efsw.hpp>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "editor/systems/editor_asset.h"
#include "engine/core/file_system_utils.h"
#include "engine/core/logger.h"
#include "engine/resources/concurrent_queue.h"

class ProjectObserver {
public:
  struct IONode {
    inline static uint32_t id_counter_;

    // session-unique id of IO node
    uint32_t id_;

    // node name
    std::string name_;

    // Path of IO node, relative to project root
    FileSystem::Path path_;

    explicit IONode(const std::string& name, const FileSystem::Path& path)
        : id_(++id_counter_), name_(name), path_(path) {}

    virtual bool IsFolder() = 0;
    virtual ~IONode() = default;
  };

  struct File : public IONode {

    // Asset session ID associated with file
    AssetSID asset_id_;

    explicit File(const std::string& name, const FileSystem::Path& path);

    // Link file to new editor asset
    void MakeAsset();

    // Link file to existing editor asset
    void LinkAsset(AssetSID id);

    // Destroy editor asset being linked to file
    void DestroyAsset();

    bool IsFolder() override { return false; }
  };

  struct Folder : public IONode {
    std::vector<std::shared_ptr<File>> files_;
    std::vector<std::shared_ptr<Folder>> subfolders_;
    uint32_t parent_id_;
    bool expanded_;

    explicit Folder(std::string name, FileSystem::Path path, uint32_t parent_id)
        : IONode(name, path),
          files_(),
          subfolders_(),
          parent_id_(parent_id),
          expanded_(false) {}

    ~Folder() override {}

    bool IsFolder() override { return true; }

    // Return if folder has subfolders
    bool HasSubfolders();

    // Return if folder has a subfolder with the given name
    bool HasSubfolder(const std::string& folder_name);

    // Add a file
    std::shared_ptr<File> AddFile(const std::string& file_name);

    // Add a subfolder
    std::shared_ptr<Folder> AddFolder(const std::string& folder_name);

    // Remove a file by name if existing
    void RemoveFile(const std::string& file_name, bool destroy_asset);

    // Remove subfolder by name if existing
    void RemoveFolder(const std::string& folder_name);

    // DEBUG: Print folder content recursively
    void Print(uint32_t depth = 0);
  };

  struct IOEvent {
    efsw::Action action_;
    std::string directory_;
    std::string filename_;
    std::string old_filename_;

    IOEvent(efsw::Action action, std::string directory, std::string filename,
            std::string old_filename)
        : action_(action),
          directory_(directory),
          filename_(filename),
          old_filename_(old_filename) {}
  };

  ProjectObserver();

  // Dispatch observer related events
  void PollEvents();

  // Update path of project to observe
  void SetTarget(const FileSystem::Path& path);

  // Returns project structures root folder node
  const std::shared_ptr<Folder>& RootNode();

  // Return project structure root folderby id, nullptr if invalid
  std::shared_ptr<const Folder> FetchFolder(uint32_t id);

  // Add a file the editor just wrote (and its missing folders) without
  // waiting for the file watcher. Path is relative to the project root
  void Register(const FileSystem::Path& relative_path);

private:
  // Listen to file watcher events
  struct IOListener : public efsw::FileWatchListener {
    void handleFileAction(efsw::WatchID watch_id, const std::string& directory,
                          const std::string& filename, efsw::Action action,
                          const std::string& old_filename) override;
    ConcurrentQueue<std::unique_ptr<IOEvent>> event_queue_;
  };

  // Create root folder structure for given path recursively
  std::shared_ptr<Folder> CreateFolder(const FileSystem::Path& path,
                                       uint32_t parent_id = 0);

  // Find folder node given a relative path
  std::shared_ptr<ProjectObserver::Folder> FindFolder(
      const FileSystem::Path& relative_path,
      const std::shared_ptr<Folder>& current_folder);

  // Add a file node to the folder unless it already has one: the watcher
  // may report files that were already registered
  void AddFileOnce(const std::shared_ptr<Folder>& folder,
                   const std::string& file_name, AssetSID asset_id = 0);

  // Set hidden files
  bool ShouldIgnore(const FileSystem::Path& path, bool is_directory);

  // Project root path being observed
  FileSystem::Path target_;

  // Root node of project structure representation
  std::shared_ptr<Folder> project_structure_;

  // Registry for all folders in project structure representation
  inline static std::unordered_map<uint32_t, const std::shared_ptr<Folder>>
      folder_registry_;

  std::unique_ptr<efsw::FileWatcher> watcher_;
  std::unique_ptr<IOListener> listener_;
  efsw::WatchID watch_id_;
};

#endif  // PROJECT_OBSERVER_H