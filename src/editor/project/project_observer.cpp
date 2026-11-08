#include "project_observer.h"

#include "editor/gui/asset_browser.h"
#include "editor/runtime/runtime.h"
#include "engine/core/logger.h"

ProjectObserver::File::File(const std::string& name,
                            const FileSystem::Path& path)
    : IONode(name, path), asset_id_(0) {}

void ProjectObserver::File::MakeAsset() {
  asset_id_ = Runtime::Project().Assets().Load(path_);
}

void ProjectObserver::File::LinkAsset(AssetSID id) {
  asset_id_ = id;
}

void ProjectObserver::File::DestroyAsset() {
  if (!asset_id_)
    return;
  Runtime::Project().Assets().Remove(asset_id_);
  asset_id_ = 0;
}

bool ProjectObserver::Folder::HasSubfolders() {
  return !subfolders_.empty();
}

bool ProjectObserver::Folder::HasSubfolder(const std::string& folder_name) {
  return std::any_of(
      subfolders_.begin(), subfolders_.end(),
      [&](const auto& folder) { return folder->name_ == folder_name; });
}

std::shared_ptr<ProjectObserver::File> ProjectObserver::Folder::AddFile(
    const std::string& file_name) {
  // Instantiate file
  FileSystem::Path new_path = path_ / file_name;
  auto file = std::make_shared<File>(file_name, new_path);

  // Insert file (sorted by name)
  auto it = std::lower_bound(
      files_.begin(), files_.end(), file,
      [](const std::shared_ptr<File>& a, const std::shared_ptr<File>& b) {
        return a->name_ < b->name_;
      });
  files_.insert(it, file);

  return file;
}

std::shared_ptr<ProjectObserver::Folder> ProjectObserver::Folder::AddFolder(
    const std::string& folder_name) {
  // Instantiate folder
  FileSystem::Path new_path = path_ / folder_name;
  auto folder = std::make_shared<Folder>(folder_name, new_path, id_);

  // Insert folder (sorted by name)
  auto it = std::lower_bound(
      subfolders_.begin(), subfolders_.end(), folder,
      [](const std::shared_ptr<Folder>& a, const std::shared_ptr<Folder>& b) {
        return a->name_ < b->name_;
      });
  subfolders_.insert(it, folder);
  folder_registry_.emplace(folder->id_, folder);

  return folder;
}

void ProjectObserver::Folder::RemoveFile(const std::string& file_name,
                                         bool destroy_asset) {
  auto it = std::find_if(files_.begin(), files_.end(),
                         [&](const std::shared_ptr<File>& file) {
                           return file->name_ == file_name;
                         });

  if (it != files_.end()) {
    if (destroy_asset)
      (*it)->DestroyAsset();
    files_.erase(it);
  }
}

void ProjectObserver::Folder::RemoveFolder(const std::string& folder_name) {
  auto it = std::find_if(subfolders_.begin(), subfolders_.end(),
                         [&](const std::shared_ptr<Folder>& folder) {
                           return folder->name_ == folder_name;
                         });

  if (it != subfolders_.end()) {
    auto folder_to_remove = *it;
    folder_registry_.erase(folder_to_remove->id_);
    subfolders_.erase(it);
  }
}

void ProjectObserver::Folder::Print(uint32_t depth) {
  std::string indent(depth * 3, ' ');
  std::string line(name_.size(), '-');

  Logger::getInstance().Log(LogLevel::Debug, indent + "--" + line + "--");
  Logger::getInstance().Log(LogLevel::Debug, indent + "| " + name_ + " |");
  Logger::getInstance().Log(LogLevel::Debug, indent + "--" + line + "--");

  for (const auto& file : files_) {
    Logger::getInstance().Log(LogLevel::Debug, indent + "- " + file->name_);
  }

  for (const auto& subfolder : subfolders_) {
    subfolder->Print(depth + 1);
  }
}

ProjectObserver::ProjectObserver()
    : target_(),
      project_structure_(nullptr),
      watcher_(std::make_unique<efsw::FileWatcher>()),
      listener_(std::make_unique<IOListener>()),
      watch_id_(0) {}

void ProjectObserver::PollEvents() {
  // Fetch next IO listener event
  std::unique_ptr<IOEvent> event;
  bool success = listener_->event_queue_.try_dequeue(event);
  if (!success)
    return;

  // DEBUG
  Logger::getInstance().Log(
      LogLevel::Debug,
      "ProjectObserver: Processing file event: " +
          std::string(event->action_ == efsw::Action::Add        ? "Add"
                      : event->action_ == efsw::Action::Delete   ? "Delete"
                      : event->action_ == efsw::Action::Modified ? "Modified"
                                                                 : "Moved") +
          " for file: " + event->filename_);

  // Get event related data
  std::string filename = event->filename_;
  FileSystem::Path absolute_path =
      FileSystem::Path(event->directory_) / event->filename_;
  FileSystem::Path relative_path =
      FileSystem::GetRelativePath(absolute_path, target_);
  const bool is_directory = FileSystem::IsDirectory(absolute_path);

  // Hidden, sidecar and temporary files never show. A temporary file renamed
  // into place (atomic save) still needs its move handled below
  if (event->action_ != efsw::Action::Moved &&
      ShouldIgnore(absolute_path, is_directory))
    return;

  switch (event->action_) {
    case efsw::Action::Add: {
      // Find parent folder of action
      auto parent_folder =
          FindFolder(relative_path.parent_path(), project_structure_);
      if (!parent_folder)
        break;

      // Add folder or file, unless already registered
      if (is_directory) {
        if (!parent_folder->HasSubfolder(filename))
          parent_folder->AddFolder(filename);
      } else {
        AddFileOnce(parent_folder, filename);
      }

      break;
    }
    case efsw::Action::Delete: {
      // Find parent folder of action
      auto parent_folder =
          FindFolder(relative_path.parent_path(), project_structure_);
      if (!parent_folder)
        break;

      // Remove folder or file
      parent_folder->RemoveFolder(filename);
      parent_folder->RemoveFile(filename, true);

      break;
    }
    case efsw::Action::Modified: {
      // Find parent folder of action
      auto parent_folder =
          FindFolder(relative_path.parent_path(), project_structure_);
      if (!parent_folder)
        break;

      // Find modified file
      auto file_it = std::find_if(
          parent_folder->files_.begin(), parent_folder->files_.end(),
          [&filename](const auto& file) { return file->name_ == filename; });

      // Reload asset if available. Some platforms report a file replaced by
      // a rename as modified, so add it if it isn't known yet
      if (file_it != parent_folder->files_.end()) {
        auto modified_file = *file_it;
        if (modified_file->asset_id_)
          Runtime::Project().Assets().Reload(modified_file->asset_id_);
      } else if (!is_directory) {
        AddFileOnce(parent_folder, filename);
      }

      break;
    }
    case efsw::Action::Moved: {
      // Find old parent folder of action
      FileSystem::Path old_relative_path(event->old_filename_);
      std::string old_filename = old_relative_path.filename().string();
      auto old_parent_folder =
          FindFolder(old_relative_path.parent_path(), project_structure_);
      if (!old_parent_folder)
        break;

      // Find last asset SID if moved object is a file
      AssetSID last_sid = 0;
      auto file_it = std::find_if(old_parent_folder->files_.begin(),
                                  old_parent_folder->files_.end(),
                                  [&old_filename](const auto& file) {
                                    return file->name_ == old_filename;
                                  });

      if (file_it != old_parent_folder->files_.end()) {
        last_sid = (*file_it)->asset_id_;

        // Remove old file without destroying linked asset
        old_parent_folder->RemoveFile(old_filename, false);
      } else {
        // Remove old folder
        old_parent_folder->RemoveFolder(old_filename);
      }

      // Find new parent folder of action
      std::string new_base_name = absolute_path.filename().string();
      auto new_parent_folder =
          FindFolder(relative_path.parent_path(), project_structure_);
      if (!new_parent_folder)
        break;

      // Moved to a hidden or temporary name: only the removal matters
      if (ShouldIgnore(absolute_path, is_directory))
        break;

      // Recreate folder or file
      if (is_directory) {
        if (!new_parent_folder->HasSubfolder(new_base_name))
          new_parent_folder->AddFolder(new_base_name);
      } else {
        AddFileOnce(new_parent_folder, new_base_name, last_sid);
        if (last_sid)
          Runtime::Project().Assets().UpdateLocation(last_sid, absolute_path);
      }

      break;
    }
  }
}

void ProjectObserver::AddFileOnce(const std::shared_ptr<Folder>& folder,
                                  const std::string& file_name,
                                  AssetSID asset_id) {
  const bool known = std::any_of(
      folder->files_.begin(), folder->files_.end(),
      [&](const auto& file) { return file->name_ == file_name; });
  if (known)
    return;

  auto file = folder->AddFile(file_name);
  if (asset_id)
    file->LinkAsset(asset_id);
  else
    file->MakeAsset();
}

void ProjectObserver::Register(const FileSystem::Path& relative_path) {
  if (!project_structure_)
    return;

  // Walk down to the file's folder, creating the folders it is missing
  std::shared_ptr<Folder> folder = project_structure_;
  for (const auto& part : relative_path.parent_path()) {
    const std::string name = part.string();
    auto it = std::find_if(
        folder->subfolders_.begin(), folder->subfolders_.end(),
        [&](const auto& subfolder) { return subfolder->name_ == name; });
    folder = it != folder->subfolders_.end() ? *it : folder->AddFolder(name);
  }

  AddFileOnce(folder, relative_path.filename().string());
}

void ProjectObserver::SetTarget(const FileSystem::Path& path) {
  target_ = path;

  folder_registry_.clear();
  project_structure_ = CreateFolder(target_);

  // select root folder in asset browser when project is loaded
  if (project_structure_)
    Panels::AssetBrowserPanel::SelectFolder(project_structure_->id_);

  if (watch_id_)
    watcher_->removeWatch(watch_id_);

  watch_id_ = watcher_->addWatch(target_.string(), listener_.get(), true);

  if (!watch_id_)
    Logger::getInstance().Log(
        LogLevel::Warning,
        "Project Observer: Could not initiate file watcher process");

  watcher_->watch();
}

const std::shared_ptr<ProjectObserver::Folder>& ProjectObserver::RootNode() {
  return project_structure_;
}

std::shared_ptr<const ProjectObserver::Folder> ProjectObserver::FetchFolder(
    uint32_t id) {
  auto it = folder_registry_.find(id);
  if (it != folder_registry_.end())
    return it->second;
  return nullptr;
}

void ProjectObserver::IOListener::handleFileAction(
    efsw::WatchID watch_id, const std::string& directory,
    const std::string& filename, efsw::Action action,
    const std::string& old_filename) {
  auto event =
      std::make_unique<IOEvent>(action, directory, filename, old_filename);
  bool success = event_queue_.try_enqueue(std::move(event));
  if (!success)
    Logger::getInstance().Log(
        LogLevel::Warning, "Project Observer: Failed to enqueue an IO event");
}

std::shared_ptr<ProjectObserver::Folder> ProjectObserver::CreateFolder(
    const FileSystem::Path& path, uint32_t parent_id) {
  auto root =
      std::make_shared<Folder>(path.filename().string(), path, parent_id);
  folder_registry_.emplace(root->id_, root);

  for (const auto& entry : std::filesystem::directory_iterator(path)) {
    if (ShouldIgnore(entry.path(), entry.is_directory()))
      continue;
    if (entry.is_directory())
      root->subfolders_.push_back(CreateFolder(entry.path(), root->id_));
    else if (entry.is_regular_file())
      root->AddFile(entry.path().filename().string())->MakeAsset();
  }

  return root;
}

std::shared_ptr<ProjectObserver::Folder> ProjectObserver::FindFolder(
    const FileSystem::Path& relative_path,
    const std::shared_ptr<Folder>& current_folder) {
  if (!current_folder) {
    Logger::getInstance().Log(
        LogLevel::Warning,
        "Project Observer: Attempted to search from an invalid folder");
    return nullptr;
  }

  // No path left, return current folder
  if (relative_path.empty() || relative_path == ".")
    return current_folder;

  // First component in relative path is immediate subfolder name
  auto it = relative_path.begin();
  std::string target_name = it->string();

  // Find subfolder with target name
  for (const auto& subfolder : current_folder->subfolders_) {
    if (subfolder->name_ == target_name) {
      // Compute remaining path by advancing iterator
      FileSystem::Path remaining;
      for (++it; it != relative_path.end(); ++it)
        remaining /= *it;
      // Recurse finding target folder
      return FindFolder(remaining, subfolder);
    }
  }

  // Folder not found
  Logger::getInstance().Log(
      LogLevel::Warning, "Project Observer: Some parent folder was not found");
  return nullptr;
}

bool ProjectObserver::ShouldIgnore(const FileSystem::Path& path,
                                   bool is_directory) {
  const std::string filename = path.filename().string();

  // Handle exception: project config is hidden but we track it
  if (filename == ".project")
    return false;

  // Hidden files/folders
  if (!filename.empty() && filename[0] == '.')
    return true;

  // Windows OS-generated
  if (filename == "Thumbs.db" || filename == "desktop.ini")
    return true;

  // Version control directories
  if (is_directory &&
      (filename == ".git" || filename == ".svn" || filename == ".hg"))
    return true;  // redundant with the '.' rule but explicit for clarity

  // Engine sidecar files
  if (path.extension() == ".meta" || path.extension() == ".tmp")
    return true;

  return false;
}