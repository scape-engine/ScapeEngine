#ifndef EDITOR_SETTINGS_H
#define EDITOR_SETTINGS_H

#include <string>
#include <vector>

#include "editor/gui/viewport/viewport_settings.h"

// Per-user editor settings, shared by every project
struct EditorSettings {
  // Absolute project paths, most recent first
  std::vector<std::string> recent_projects;
  Viewport::ViewportSettings viewport;

  // Load from the user settings directory, defaults if missing or invalid
  static EditorSettings Load();
  bool Save() const;

  // Move a project to the front of recent_projects
  void AddRecentProject(const std::string& path);
};

#endif  // EDITOR_SETTINGS_H
