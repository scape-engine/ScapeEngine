#include "scene_modals.h"

#include <imgui.h>
#include <cstdio>
#include <string>
#include <utility>

#include "editor/runtime/runtime.h"

static std::function<void()> s_pending_action;
// Clean scene: run the pending action at the start of the next Render(),
// before panels draw entities it may destroy
static bool s_run_pending = false;
static bool s_open_unsaved = false;
static bool s_open_save_as = false;
static char s_scene_name[128] = "";
static std::string s_save_as_error;

// Why a scene name can't be used, empty if it can
static std::string _ValidateSceneName(const std::string& name) {
  if (name.empty())
    return "Enter a scene name";
  if (name.front() == '.' ||
      name.find_first_of("/\\:*?\"<>|") != std::string::npos)
    return "Scene names can't start with '.' or contain / \\ : * ? \" < > |";

  // Never overwrite another scene
  ProjectManager& project = Runtime::Project();
  const auto path = project.ScenePathFromName(name);
  if (path != project.ScenePath() &&
      FileSystem::Exists(project.AbsolutePath(path)))
    return "A scene named '" + name + "' already exists";
  return "";
}

static void _RunPending() {
  std::function<void()> action = std::move(s_pending_action);
  s_pending_action = nullptr;
  if (action)
    action();
}

void SceneModals::RunAfterSaveCheck(std::function<void()> action) {
  s_pending_action = std::move(action);
  if (Runtime::Project().IsSceneDirty())
    s_open_unsaved = true;
  else
    s_run_pending = true;
}

void SceneModals::RequestSave() {
  if (Runtime::Project().ScenePath().empty())
    s_open_save_as = true;
  else
    Runtime::Project().SaveScene();
}

void SceneModals::RequestSaveAs() {
  s_open_save_as = true;
}

void SceneModals::Render() {
  ProjectManager& project = Runtime::Project();

  if (std::exchange(s_run_pending, false))
    _RunPending();

  if (std::exchange(s_open_unsaved, false))
    ImGui::OpenPopup("Unsaved Changes");
  if (std::exchange(s_open_save_as, false)) {
    // Start from the open scene's name
    const std::string name = project.ScenePath().empty()
                                 ? std::string("NewScene")
                                 : project.SceneName();
    std::snprintf(s_scene_name, sizeof(s_scene_name), "%s", name.c_str());
    s_save_as_error.clear();
    ImGui::OpenPopup("Save Scene As");
  }

  if (ImGui::BeginPopupModal("Unsaved Changes", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Save changes to the current scene?");
    if (ImGui::Button("Save")) {
      ImGui::CloseCurrentPopup();
      if (project.ScenePath().empty())
        s_open_save_as = true;  // runs the pending action once saved
      else if (project.SaveScene())
        _RunPending();
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard")) {
      ImGui::CloseCurrentPopup();
      _RunPending();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
      s_pending_action = nullptr;
    }
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("Save Scene As", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    // TODO: native file dialog (scenes are currently always saved in the
    // project's scenes folder)
    ImGui::TextUnformatted("Scene name");
    if (ImGui::IsWindowAppearing())
      ImGui::SetKeyboardFocusHere();
    const bool submitted =
        ImGui::InputText("##SceneName", s_scene_name, sizeof(s_scene_name),
                         ImGuiInputTextFlags_EnterReturnsTrue);

    if (!s_save_as_error.empty())
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                         s_save_as_error.c_str());

    if (ImGui::Button("Save") || submitted) {
      const std::string name = s_scene_name;
      s_save_as_error = _ValidateSceneName(name);
      if (s_save_as_error.empty()) {
        if (project.SaveSceneAs(project.ScenePathFromName(name))) {
          ImGui::CloseCurrentPopup();
          _RunPending();
        } else {
          s_save_as_error = "Couldn't save the scene, see the console";
        }
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
      s_pending_action = nullptr;
    }
    ImGui::EndPopup();
  }
}
