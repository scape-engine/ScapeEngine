#ifndef EDITOR_UI_H
#define EDITOR_UI_H

#include <entt/entt.hpp>
#include <memory>

#define IMGUI_DEFINE_MATH_OPERATORS  // ! remove once EditorApp is implemented
#include "imgui.h"

#include "editor/camera/god_camera.h"
#include "editor/gizmos/component_gizmos.h"
#include "editor/gui/search/search_popup.h"
#include "editor_base.h"
#include "engine/pcg/procmodel/preview_data.h"  // ! remove once EditorApp is implemented

#include "engine/renderer/graphics_renderer.h"

#include "game/game_test.h"

#include "platform/input/key_map_sdl.h"

struct ImGuiDockNode;

/**************************************************************************
 * EditorUI
 * -------
 * Editor layer
 * Currently manages ImGui context, docking, window registry, rendering, and
 * application loop
 *
 * TODO:
 *  - Decouple EditorUI into EditorUI (UI framework only), and EditorApp
 * (application loop)
 **************************************************************************/

class EditorUI : public EditorBase {
public:
  explicit EditorUI(RendererBase* renderer);
  ~EditorUI();

  static EditorUI* Get();  // Singleton access

  // Generate unique ImGui ID string
  std::string GenerateIdString();

  // Generaye unique ImGui ID
  ImGuiID GenerateId();

  void Initialize();
  void Run() override;
  void RequestUpdate() override;
  void Destroy() override;
  void BeginImGuiFrame(SDL_Window* window);

private:
  void HandleInput(Key key);
  void RenderUI();
  void DockSpace();
  void LoadIcons();
  void UpdateMovement();

  void DrawPanelCards(ImGuiDockNode* node);

  template <typename T, typename... Args>
  T* _AddWindow(Args&&... args);

  RendererBase* renderer_ = nullptr;
  bool quit_ = false;
  bool is_game_started_ = true;
  bool game_canvas_hovered_ = false;
  bool game_canvas_focused_ = false;
  bool built_layout_ = false;  // guard for BuildDefaultLayout()

  // Id counter
  uint32_t g_id_counter_;

  // ImGui docking and window flags
  ImGuiDockNodeFlags dock_flags_;
  ImGuiWindowFlags host_flags_;
  ImGuiID dock_id_;

  // singleton pointer
  static EditorUI* s_instance_;

  // ImGui debug controls
  bool debug_highlight_ids_ = false;
  bool debug_show_metrics_ = false;
  bool debug_show_log_ = false;
  bool debug_activate_picker_ = false;
  bool debug_show_style_editor_ = false;

  // Map component names to icons
  std::unordered_map<std::string, std::string> tab_icons_;

  GodCameraState god_state_{};
  PreviewData procmodel_data_;
};

#endif  // EDITOR_UI_H
