#include "project_manager.h"

#include <nlohmann/json.hpp>
#include <sstream>

#include "editor/events.h"
#include "engine/content/io/json.h"
#include "engine/core/logger.h"
#include "engine/ecs/world.h"
#include "engine/scene/model_entity_factory.h"
#include "engine/scene/scene_serializer.h"
#include "engine/scene/scene_template.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Project::Config, name, author,
                                                tags, version, startup_scene)

// Folder holding the project's scenes, relative to the project root
static const std::filesystem::path scenes_folder = "scenes";

// Scene every project starts with
static const std::filesystem::path sample_scene =
    scenes_folder / "SampleScene.scene";

// Config format used before .project was JSON
static void _ParseLegacyConfig(const std::string& source,
                               Project::Config& cfg) {
  std::istringstream stream(source);
  std::string line;
  while (std::getline(stream, line)) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos)
      continue;
    const std::string key = line.substr(0, eq);
    const std::string value = line.substr(eq + 1);
    if (key == "name")
      cfg.name = value;
    else if (key == "author")
      cfg.author = value;
    else if (key == "tags")
      cfg.tags = value;
    else if (key == "version")
      cfg.version = value;
  }
}

ProjectManager::ProjectManager() : project_(), observer_(), assets_() {}

void ProjectManager::PollEvents() {
  observer_.PollEvents();
}

bool ProjectManager::Load(const std::filesystem::path& directory) {
  if (!std::filesystem::exists(directory) ||
      !std::filesystem::is_directory(directory)) {
    Logger::getInstance().Log(LogLevel::Error,
                              "Project path is invalid. Provided path was '" +
                                  directory.string() + "'");
    return false;
  }

  // Set project path
  project_.path_ = directory;

  // Ensure project configuration is valid
  if (!EnsureConfig()) {
    Logger::getInstance().Log(LogLevel::Error,
                              "Failed to ensure project configuration.");
    return false;
  }

  // Drop the previous project's assets, then start observing this one
  assets_.Clear();
  observer_.SetTarget(project_.path_);

  OpenStartupScene();
  return true;
}

bool ProjectManager::Save() const {
  return Content::WriteJsonFile((project_.path_ / ".project").string(),
                                nlohmann::json(project_.config));
}

const Project& ProjectManager::GetProject() const {
  return project_;
}

ProjectObserver& ProjectManager::GetObserver() {
  return observer_;
}

ProjectAssets& ProjectManager::Assets() {
  return assets_;
}

std::filesystem::path ProjectManager::AbsolutePath(
    const std::filesystem::path& path) {
  return project_.path_ / path;
}

std::filesystem::path ProjectManager::RelativePath(
    const std::filesystem::path& path) const {
  return path.is_absolute() ? path.lexically_relative(project_.path_) : path;
}

bool ProjectManager::EnsureConfig() {
  const std::filesystem::path config_path = project_.path_ / ".project";
  Project::Config& cfg = project_.config;
  cfg = Project::Config();
  bool needs_save = true;

  if (FileSystem::Exists(config_path)) {
    const std::string source = FileSystem::ReadFile(config_path);
    const size_t first = source.find_first_not_of(" \t\r\n");

    if (first != std::string::npos && source[first] == '{') {
      // Never overwrite a JSON config that fails to parse, it may be fixed
      // by hand
      needs_save = false;
      try {
        cfg = nlohmann::json::parse(source).get<Project::Config>();
      } catch (const nlohmann::json::exception& e) {
        Logger::getInstance().Log(LogLevel::Error,
                                  "Invalid project configuration '" +
                                      config_path.string() +
                                      "', using defaults: " + e.what());
      }
    } else {
      // Legacy key=value config, migrated to JSON below
      _ParseLegacyConfig(source, cfg);
    }
  }

  if (cfg.name.empty())
    cfg.name = project_.path_.filename().string();

  InvalidateSize();
  return !needs_save || Save();
}

// TODO: rework function, project name and
// folder name should be handled separetely
std::string ProjectManager::ProjectName() const {
  return project_.config.name;
}

uint64_t ProjectManager::SizeOnDisk() {
  if (size_cached_)
    return size_bytes_;
  size_bytes_ = 0;
  std::error_code ec;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(
           project_.path_,
           std::filesystem::directory_options::skip_permission_denied, ec)) {
    if (entry.is_regular_file(ec))
      size_bytes_ += entry.file_size(ec);
  }
  size_cached_ = true;
  return size_bytes_;
}

/* ---------- scenes ---------- */
void ProjectManager::NewScene() {
  ECS::Main().Clear();
  SceneTemplate().Create();
  scene_path_.clear();
  scene_dirty_ = false;
  EditorEvents::scene_opened();
}

void ProjectManager::OpenStartupScene() {
  std::filesystem::path startup = project_.config.startup_scene;
  if (startup.empty() || !FileSystem::Exists(AbsolutePath(startup)))
    startup = sample_scene;

  if (FileSystem::Exists(AbsolutePath(startup))) {
    // Never overwrite an unreadable scene: start an unsaved one instead
    if (!OpenScene(startup)) {
      NewScene();
      return;
    }
  } else {
    // New project, or its startup scene was deleted: create the sample scene
    NewScene();
    if (!SaveSceneAs(startup))
      return;
  }

  if (project_.config.startup_scene != startup.generic_string()) {
    project_.config.startup_scene = startup.generic_string();
    Save();
  }
}

bool ProjectManager::OpenScene(const std::filesystem::path& path) {
  if (path.empty())
    return false;

  auto scene = Content::ReadJsonFile(AbsolutePath(path).string());
  if (!scene ||
      !SceneSerializer::Deserialize(ECS::Main(), *scene, project_.path_))
    return false;
  ModelEntityFactory::BindMeshRenderers(ECS::Main(), project_.path_);

  scene_path_ = RelativePath(path);
  scene_dirty_ = false;
  EditorEvents::scene_opened();
  return true;
}

bool ProjectManager::SaveScene() {
  if (scene_path_.empty())
    return false;

  const nlohmann::json scene =
      SceneSerializer::Serialize(ECS::Main(), project_.path_);
  if (!Content::WriteJsonFile(AbsolutePath(scene_path_).string(), scene))
    return false;

  // Show the file right away, without waiting for the file watcher
  observer_.Register(scene_path_);
  scene_dirty_ = false;
  return true;
}

std::filesystem::path ProjectManager::ScenePathFromName(
    const std::string& name) const {
  return scenes_folder / (name + ".scene");
}

std::string ProjectManager::SceneName() const {
  return scene_path_.empty() ? "Untitled" : scene_path_.stem().string();
}

bool ProjectManager::SaveSceneAs(const std::filesystem::path& path) {
  std::filesystem::path file = RelativePath(path);
  if (file.extension() != ".scene")
    file += ".scene";

  std::error_code ec;
  std::filesystem::create_directories(AbsolutePath(file).parent_path(), ec);

  const std::filesystem::path previous = scene_path_;
  scene_path_ = file;
  if (!SaveScene()) {
    scene_path_ = previous;
    return false;
  }

  // The first saved scene becomes the one the project opens with
  if (project_.config.startup_scene.empty()) {
    project_.config.startup_scene = file.generic_string();
    Save();
  }
  return true;
}

// TODO: runtime save games: reuse SceneSerializer plus a gameplay-state
// block, saved under the user settings path
