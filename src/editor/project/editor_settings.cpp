#include "editor_settings.h"

#include <SDL.h>
#include <algorithm>
#include <nlohmann/json.hpp>

#include "engine/content/io/json.h"
#include "engine/core/file_system_utils.h"
#include "engine/core/logger.h"

namespace Viewport {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GridSettings, enabled,
                                                cell_size, major_every,
                                                fade_start, fade_end, opacity,
                                                axis_lines)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    GizmoSettings, enabled, selection_outline, selection_wireframe,
    billboard_opacity, billboard_size, billboard_depth_aware, billboard_camera,
    billboard_light, camera_gizmos, model_gizmos, light_gizmos, sound_gizmos)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(OverlaySettings, stats,
                                                notifications, axis_gizmo)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ViewportSettings, view_mode,
                                                grid, gizmos, overlays)
}  // namespace Viewport

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EditorSettings,
                                                recent_projects, viewport)

static constexpr size_t max_recent_projects = 10;

static std::string _SettingsPath() {
  // Per-user, writable directory (e.g. ~/Library/Application Support/...)
  char* directory = SDL_GetPrefPath("Fractal", "Editor");
  if (!directory)
    return "editor_settings.json";
  std::string path = std::string(directory) + "editor_settings.json";
  SDL_free(directory);
  return path;
}

EditorSettings EditorSettings::Load() {
  const std::string path = _SettingsPath();
  if (!FileSystem::Exists(path))
    return EditorSettings();

  auto json = Content::ReadJsonFile(path);
  if (!json)
    return EditorSettings();

  try {
    return json->get<EditorSettings>();
  } catch (const nlohmann::json::exception& e) {
    Logger::getInstance().Log(LogLevel::Warning,
                              "Invalid editor settings, using defaults: " +
                                  std::string(e.what()));
    return EditorSettings();
  }
}

bool EditorSettings::Save() const {
  return Content::WriteJsonFile(_SettingsPath(), nlohmann::json(*this));
}

void EditorSettings::AddRecentProject(const std::string& path) {
  recent_projects.erase(
      std::remove(recent_projects.begin(), recent_projects.end(), path),
      recent_projects.end());
  recent_projects.insert(recent_projects.begin(), path);
  if (recent_projects.size() > max_recent_projects)
    recent_projects.resize(max_recent_projects);
}
