#include "editor/editor_ui.h"
#include "editor/events.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/runtime/runtime.h"
#include "engine/context/engine_context.h"
#include "engine/core/engine_globals.h"
#include "engine/core/logger.h"
#include "engine/core/view_ids.h"
#include "engine/ecs/world.h"
#include "engine/pcg/pcg_engine.h"
#include "gui/asset_browser.h"
#include "gui/asset_graph_editor.h"
#include "gui/console_panel.h"
#include "gui/file_explorer.h"
#include "gui/game_canvas.h"
#include "gui/hierarchy_panel.h"
#include "gui/info_panel.h"
#include "gui/inspector_panel.h"
#include "gui/menu_bar.h"
#include "gui/model_preview.h"
#include "gui/model_viewer.h"
#include "gui/pcg_graph_editor.h"
#include "gui/scene_panel.h"
#include "gui/search/search_popup.h"
#include "gui/status_bar.h"
#include "gui/tab_container.h"
#include "gui/terrain_editor_panel.h"
#include "gui/toolbar.h"
#include "platform/platform_utils.h"
#include "platform/window_manager.h"
#include "resources/decorators/drop_shadows.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_utils.h"

#include <backends/imgui_impl_sdl2.h>
#include "editor/vendor/imgui/imgui_impl_bgfx.h"

#include <SDL.h>

// static AssetGraphEditor g_asset_graph_editor;

// Window base class
std::vector<WindowBase*> g_windows_;

EditorUI* EditorUI::s_instance_ = nullptr;

EditorUI::EditorUI(RendererBase* renderer) : renderer_(renderer) {
  s_instance_ = this;
}

EditorUI::~EditorUI() {
  s_instance_ = nullptr;  // clear on destroy
}

EditorUI* EditorUI::Get() {
  return s_instance_;
}

void EditorUI::Initialize() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  // Bump stale ini file
  static constexpr const char* layout_ini = "editor_v2.ini";
  ImGui::GetIO().IniFilename = layout_ini;

  EditorStyles::Initialize();
  LoadIcons();

  // Set up window title
  ImVec4 bar = ImGui::ColorConvertU32ToFloat4(EditorColor::panel);
  Platform::StyleTitleBar(WindowManager::GetWindow(), bar.x, bar.y, bar.z);

  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;    // Enable Docking
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;  // Enable Multi-Viewport

  io.ConfigViewportsNoAutoMerge = true;
  io.ConfigViewportsNoTaskBarIcon = true;

  // handle docking behavior
  // ImGui::GetStyle().WindowMenuButtonPosition = ImGuiDir_None;

  // backends
  ImGui_Implbgfx_Init(ViewID::UI);
  ApplyImGuiShaders();
  ImGui_ImplSDL2_InitForOther(WindowManager::GetWindow());

  io.DisplaySize = ImVec2((float)WindowManager::GetWidth(),
                          (float)WindowManager::GetHeight());
  io.DisplayFramebufferScale =
      ImVec2(WindowManager::GetDPIScale(), WindowManager::GetDPIScale());

  // ADD DEFAULT EDITOR WINDOWS
  std::vector<TabEntry> property_tabs;
  property_tabs.push_back({ICON_FA_CIRCLE_INFO, "Project Info", "Project Info",
                           std::make_unique<InfoPanel>(), 0});
  property_tabs.push_back({ICON_FA_EARTH_AMERICAS, "Scene", "Scene",
                           std::make_unique<ScenePanel>(), 1});
  property_tabs.push_back({ICON_FA_CUBE, "Inspector", "Inspector",
                           std::make_unique<InspectorPanel>(), 2});
  property_tabs.push_back({ICON_FA_MOUNTAIN, "Terrain Editor", "Terrain Editor",
                           std::make_unique<TerrainEditorPanel>(), 2});

  TabContainer* properties =
      _AddWindow<TabContainer>("Properties", std::move(property_tabs));
  _AddWindow<HierarchyPanel>();

  // _AddWindow<InfoPanel>();
  // _AddWindow<ScenePanel>();
  _AddWindow<PCGGraphEditorPanel>();
  // _AddWindow<InspectorPanel>();
  _AddWindow<ModelPreview>(&procmodel_data_);
  _AddWindow<ModelViewer>(&procmodel_data_);
  _AddWindow<AssetGraphEditor>(&procmodel_data_);

  // TODO: Refactor these panels to EditorBase:
  // _AddWindow<ConsolePanel>();
  // _AddWindow<AssetBrowserPanel>();

  // events
  EditorEvents::entity_selected.connect(
      [properties](Entity) { properties->Select("Inspector"); });
}

// TODO: refactor loop, should be split into EditorUI::NewFrame() /
// EditorUI::Render() Renamed to NextFrame()??
// Also create a window_viewport for SDL stuff in the loop
void EditorUI::Run() {
  Logger::getInstance().Log(LogLevel::Info, "EditorUI main loop start");

  Initialize();

  SDL_Event event;
  while (!quit_) {

    // TODO: move this out of the loop later
    EngineContext::NextFrame();

    // ── 0. SDL EVENTS ───────────────────────────────────────────
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);

      if (event.type == SDL_KEYDOWN) {
        Key key = sdl_key_to_key(event);
        if (key != Key::NONE)
          HandleInput(key);
      }

      // ── WINDOW EVENTS ────────────────────────
      if (event.type == SDL_WINDOWEVENT) {
        switch (event.window.event) {
          case SDL_WINDOWEVENT_SIZE_CHANGED:
            if (!WindowManager::IsFullscreen() && !WindowManager::minimized) {
              WindowManager::OnWindowResize(event.window.data1,
                                            event.window.data2);
              ImGui::GetIO().DisplaySize =
                  ImVec2((float)event.window.data1, (float)event.window.data2);
            }
            break;

          case SDL_WINDOWEVENT_MINIMIZED:
            WindowManager::minimized = true;
            break;

          case SDL_WINDOWEVENT_RESTORED:
            WindowManager::minimized = false;
            WindowManager::OnWindowResize(event.window.data1,
                                          event.window.data2);
            break;

          default:
            break;
        }
      }

      // toggle biorderless fullscreen with F11
      if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F11)
        WindowManager::ToggleFullscreen();

      if (event.type == SDL_QUIT) {
        quit_ = true;
        WindowManager::Quit();
        EditorEvents::editor_exit_pressed();
      }
    }  // end PollEvent loop

    // Skip while window is minimised
    if (WindowManager::minimized) {
      SDL_Delay(16);  // ~60 FPS idle
      continue;
    }

    // Process system events
    Runtime::Project().PollEvents();

    /* 1 - Clear background and initialize frame */
    bgfx::setViewClear(ViewID::UI_BACKGROUND, BGFX_CLEAR_COLOR, 0x1e1e1eff,
                       1.0f, 0);
    bgfx::touch(ViewID::UI_BACKGROUND);  // process background

    /* 2 - Build ImGui UI structure */
    BeginImGuiFrame(WindowManager::GetWindow());

    /* 3 - Prepare rendering target */
    auto* graphics = static_cast<GraphicsRenderer*>(renderer_);
    graphics->PrepareFrame();  // set up scene framebuffer view

    // Update global resources and set them in frame graph
    // TODO: check if this should be called here
    Runtime::UpdateGlobalResources();

    Runtime::GetFrameGraph().SetGlobalResources(
        Runtime::BuildGlobalResources());

    Runtime::GetSceneViewPipeline().Render();

    RenderUI();

    /* 4 - Finalize ImGui frame definition */
    ImGui::Render();  // prepare imgui draw data (no rendering yet)

    /* 5 - Render game content to framebuffer */
    if (is_game_started_) {
      Runtime::Game()->Render();  // Render 3D scene
    }

    /* 6 - Render ImGui elements */
    ImGui_Implbgfx_RenderDrawLists(ImGui::GetDrawData());  // main window UI

    /* 7 - Handle multi-viewport (detached windows) */
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();

    /* 8 - Submit frame to display */
    renderer_->Render();  // finalize rendering
    bgfx::frame();        // submit to GPU and swap buffers
  }
  Logger::getInstance().Log(LogLevel::Info, "EditorUI loop exited");
}

void EditorUI::RequestUpdate() {}

void EditorUI::Destroy() {
  Logger::getInstance().Log(LogLevel::Info, "Shutting down EditorUI");
  ImGui_Implbgfx_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
}

void EditorUI::HandleInput(Key key) {
  if (ImGui::GetIO().WantCaptureKeyboard && !game_canvas_hovered_)
    return;

  switch (key) {
    case Key::DIGIT_0:
      quit_ = true;
      WindowManager::Quit();
      EditorEvents::editor_exit_pressed();
      return;
    case Key::DIGIT_1:
      if (!is_game_started_) {
        is_game_started_ = true;
        EditorEvents::game_start_pressed();
      }
      return;
    case Key::DIGIT_2:
      if (is_game_started_) {
        is_game_started_ = false;
        EditorEvents::game_end_pressed();
      }
      return;
    default:
      break;
  }

  if (!is_game_started_)
    return;

  InputEvent input_event(key);
  if (auto* gm = Runtime::Game()) {
    input_event.pressed_frame_ = gm->GetFrameCount();
  }
  Runtime::InputDevice()->ForwardInputEvent(input_event,
                                            input_event.pressed_frame_);
}

void EditorUI::DockSpace() {
  static constexpr const char* root_dock = "##MainDockHost";

  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::SetNextWindowViewport(vp->ID);

  host_flags_ = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoFocusOnAppearing;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(
      ImGuiStyleVar_WindowPadding,
      ImVec2(EditorSizes::panel_margin, EditorSizes::panel_margin));
  ImGui::Begin(root_dock, nullptr, host_flags_);

  dock_flags_ = ImGuiDockNodeFlags_NoUndocking;
  dock_id_ = ImGui::GetID(root_dock);
  ImGui::DockSpace(dock_id_, ImVec2(0, 0), dock_flags_);
  ImGui::End();
  ImGui::PopStyleVar(3);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg,
                        ImGui::ColorConvertU32ToFloat4(EditorColor::panel));
  ImGui::PushStyleColor(ImGuiCol_MenuBarBg,
                        ImGui::ColorConvertU32ToFloat4(EditorColor::panel));
  Panels::MenuBar(
      [this]() {
        if (is_game_started_) {
          is_game_started_ = false;
          EditorEvents::game_end_pressed();
        }
        this->quit_ = true;
        WindowManager::Quit();
        EditorEvents::editor_exit_pressed();
      },
      debug_highlight_ids_, debug_show_metrics_, debug_show_log_,
      debug_activate_picker_, debug_show_style_editor_);

  Panels::StatusBar();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
}

void EditorUI::DrawPanelCards(ImGuiDockNode* node) {
  if (!node || !node->IsVisible)
    return;
  if (!node->IsLeafNode()) {
    DrawPanelCards(node->ChildNodes[0]);
    DrawPanelCards(node->ChildNodes[1]);
    return;
  }
  if (node->IsEmpty())
    return;

  ImDrawList* dl = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
  const ImVec2 mn = node->Pos;
  const ImVec2 mx =
      ImVec2(node->Pos.x + node->Size.x, node->Pos.y + node->Size.y);

  dl->AddRectFilled(mn, mx, EditorColor::panel, EditorSizes::panel_radius);

  if (node->TabBar && !node->IsHiddenTabBar() && !node->IsNoTabBar()) {
    const float y = node->TabBar->BarRect.Max.y;
    dl->AddLine(ImVec2(mn.x, y), ImVec2(mx.x, y), EditorColor::panel_stroke,
                EditorSizes::hairline);
  }
}

void EditorUI::RenderUI() {
  ImGuiIO& io = ImGui::GetIO();

  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::GetBackgroundDrawList(vp)->AddRectFilled(
      vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
      EditorColor::void_bg);

  DockSpace();

  // ——— Build layout ———
  if (!built_layout_) {
    if (ImGui::DockBuilderGetNode(dock_id_) == nullptr ||
        ImGui::DockBuilderGetNode(dock_id_)->IsEmpty()) {

      ImGui::DockBuilderRemoveNode(dock_id_);
      ImGui::DockBuilderAddNode(dock_id_, ImGuiDockNodeFlags_None);
      ImGui::DockBuilderSetNodeSize(dock_id_, ImGui::GetIO().DisplaySize);

      ImGuiID center = dock_id_;

      // splits:
      ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f,
                                                 nullptr, &center);
      ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.25f,
                                                   nullptr, &center);
      ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.20f,
                                                  nullptr, &center);
      ImGuiID top = ImGui::DockBuilderSplitNode(center, ImGuiDir_Up, 0.15f,
                                                nullptr, &center);

      // docked Panels
      ImGui::DockBuilderDockWindow("Hierarchy", left);
      ImGui::DockBuilderDockWindow("Properties", right);
      ImGui::DockBuilderDockWindow("Viewport", center);
      ImGui::DockBuilderDockWindow("Model View", center);
      ImGui::DockBuilderDockWindow("Toolbar", center);
      ImGui::DockBuilderDockWindow("Console", bottom);
      ImGui::DockBuilderDockWindow("Asset Browser", bottom);
      ImGui::DockBuilderDockWindow("PCG Graph Editor", bottom);
      ImGui::DockBuilderDockWindow("Asset Graph", bottom);
      ImGui::DockBuilderDockWindow("Procedural Preview", bottom);

      ImGui::DockBuilderFinish(dock_id_);
    }
    built_layout_ = true;
  }

  //--------------------------- TOP TOOLBAR ----------------------------------
  Panels::ToolbarCallbacks cb;

  cb.onStart = [this]() {
    if (!is_game_started_) {
      is_game_started_ = true;
      EditorEvents::game_start_pressed();
    }
  };

  cb.onStop = [this]() {
    if (is_game_started_) {
      is_game_started_ = false;
      EditorEvents::game_end_pressed();
    }
  };

  cb.onQuit = [this]() {
    if (is_game_started_) {
      is_game_started_ = false;
      EditorEvents::game_end_pressed();
    }
    this->quit_ = true;
    WindowManager::Quit();
    EditorEvents::editor_exit_pressed();
  };

  ImGui::Begin("Toolbar", nullptr);
  Panels::Toolbar(cb);
  ImGui::End();

  auto& world = ECS::Main();

  // -------- MIDDLE : SCENE --------
  ImGui::Begin("Viewport", nullptr);
  Panels::GameCanvas(is_game_started_, game_canvas_hovered_,
                     game_canvas_focused_);
  UpdateMovement();
  ImGui::End();

  //--------------------------- Panels ------------------------------
  ImGui::Begin("Console", nullptr);
  Panels::ConsolePanel();
  ImGui::End();

  ImGui::Begin("Asset Browser", nullptr);
  Panels::AssetBrowser();
  ImGui::End();
  // Panels::FileExplorer();
  //------------------------- SEARCH POPUP --------------------------
  SearchPopup::Render();

  //------------------------- IMGUI DEBUG ---------------------------
  if (debug_show_metrics_) {
    ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
    ImGui::ShowMetricsWindow(&debug_show_metrics_);
  }
  if (debug_show_log_) {
    ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
    ImGui::ShowDebugLogWindow(&debug_show_log_);
  }
  if (debug_show_style_editor_) {
    ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
    ImGui::Begin("Style Editor", &debug_show_style_editor_);
    ImGui::ShowStyleEditor();
    ImGui::End();
  }
  if (debug_activate_picker_) {
    ImGui::DebugStartItemPicker();
    debug_activate_picker_ = false;
  }

  // Push current state to pipeline for rendering
  {
    auto& state = Runtime::State();
    std::vector<Entity> state_list;
    if (state.selected_entity != entt::null &&
        world.Reg().valid(state.selected_entity)) {
      state_list.push_back(state.selected_entity);
    }
    Runtime::GetSceneViewPipeline().SetSelectedEntities(state_list);
  }

  // -------- Render all registered WindowBase windows --------
  // This automatically renders HierarchyPanel, InspectorPanel,
  // PCGGraphEditorPanel
  for (auto* window : g_windows_) {
    window->Render();
  }

  // -------- Card corners over all docked leaves (no per-panel edits) --------
  DrawPanelCards(ImGui::DockBuilderGetNode(dock_id_));
}

void EditorUI::LoadIcons() {
  tab_icons_.insert({"Hierarchy", ICON_FA_SITEMAP});
  tab_icons_.insert({"Toolbar", ICON_FA_TOOLBOX});
  tab_icons_.insert({"Viewport", ICON_FA_GAMEPAD});
  tab_icons_.insert({"Inspector", ICON_FA_LIST});
  tab_icons_.insert({"Console", ICON_FA_TERMINAL});
  tab_icons_.insert({"Assets", ICON_FA_FOLDER_OPEN});
  tab_icons_.insert({"World", ICON_FA_GLOBE});
}

void EditorUI::BeginImGuiFrame(SDL_Window* window) {
  // Reset ID counter for new frame
  g_id_counter_ = 0;

  ImGui_ImplSDL2_NewFrame();  // platform backend
  ImGui_Implbgfx_NewFrame();  // renderer backend
  ImGui::NewFrame();          // ImGui begins
  ImGuizmo::BeginFrame();     // ImGuizmo begins
  // ---- decorators start here ----
  // drop shadow effect
  // static bool shadowOn = true;
  // ui::shadow::BeginFrame(shadowOn);
}

void EditorUI::UpdateMovement() {
  // Build per-frame input
  GodCameraFrameInput input{};
  input.scene_hovered = game_canvas_hovered_;

  // Enable/disable text input
  if (input.scene_hovered) {
    Platform::DisableTextInput();
  } else {
    Platform::EnableTextInput();
  }

  input.right_mouse =
      input.scene_hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);
  input.middle_mouse =
      input.scene_hovered && ImGui::IsMouseDown(ImGuiMouseButton_Middle);

  ImGuiIO& io = ImGui::GetIO();
  input.mouse_delta = (input.right_mouse || input.middle_mouse)
                          ? glm::vec2(io.MouseDelta.x, io.MouseDelta.y)
                          : glm::vec2(0.0f);
  input.mouse_wheel = io.MouseWheel;

  // WASD axis
  if (input.scene_hovered) {
    const float fwd = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) -
                      (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
    const float sid = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) -
                      (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
    input.move_axis = glm::vec2(fwd, sid);

    // Arrow keys for camera rotation
    const float look_y = (ImGui::IsKeyDown(ImGuiKey_UpArrow) ? 1.0f : 0.0f) -
                         (ImGui::IsKeyDown(ImGuiKey_DownArrow) ? 1.0f : 0.0f);
    const float look_x = (ImGui::IsKeyDown(ImGuiKey_RightArrow) ? 1.0f : 0.0f) -
                         (ImGui::IsKeyDown(ImGuiKey_LeftArrow) ? 1.0f : 0.0f);
    input.look_axis = glm::vec2(look_x, look_y);

    // Speed boost with SHIFT
    input.hold_shift = ImGui::IsKeyDown(ImGuiKey_LeftShift) ||
                       ImGui::IsKeyDown(ImGuiKey_RightShift);
  } else {
    input.move_axis = glm::vec2(0.0f);
    input.look_axis = glm::vec2(0.0f);
    input.hold_shift = false;
  }

  // Get the ECS transform
  auto& pipeline = Runtime::GetSceneViewPipeline();
  TransformComponent& CameraTransform = std::get<0>(pipeline.GetGodCamera());

  // Run update
  GodCameraUpdateMovement(god_state_, CameraTransform, input);

  // Rebuild model matrix
  Transform::Evaluate(CameraTransform);
}

ImGuiID EditorUI::GenerateId() {
  return ++g_id_counter_;
}

std::string EditorUI::GenerateIdString() {
  return "##" + std::to_string(++g_id_counter_);
}

template <typename T, typename... Args>
T* EditorUI::_AddWindow(Args&&... args) {
  static_assert(std::is_base_of<WindowBase, T>::value,
                "Only classes deriving from WindowBase can be added!");
  T* window = new T(std::forward<Args>(args)...);
  g_windows_.emplace_back(window);
  return window;
}
