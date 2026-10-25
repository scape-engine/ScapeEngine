#ifndef GAME_CANVAS_H
#define GAME_CANVAS_H

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <imgui.h>
#include <algorithm>

#include "editor/gui/styles/editor_styles.h"
#include "editor/gui/viewport/action_overlay.h"
#include "editor/gui/viewport/axis_gizmo.h"
#include "editor/gui/viewport/stats_overlay.h"
#include "editor/gui/viewport/toolbar.h"
#include "editor/runtime/runtime.h"

#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"
#include "editor/vendor/imguizmo/ImGuizmo.h"

#include "engine/core/engine_globals.h"
#include "engine/core/logger.h"
#include "engine/core/types/material_data.h"
#include "engine/core/view_ids.h"

#include "engine/ecs/world.h"
#include "engine/math/transformation.h"

#include "engine/renderer/frame_graph.h"
#include "engine/renderer/graphics_renderer.h"
#include "engine/renderer/material/material_registry.h"
#include "engine/renderer/model/model.h"

#include "engine/core/file_system_utils.h"

// TODO: rename window to scene_view_window

namespace Panels {

struct TransformGizmoState {
  ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
  ImGuizmo::MODE mode = ImGuizmo::WORLD;
  float scale_min = 0.1f;

  // Snapping (toolbar toggle)
  bool snap_enabled = false;
  float snap_translate = 0.5f;  // world units
  float snap_rotate = 45.0f;    // degrees
  float snap_scale = 0.1f;      // scale factor step

  // Increment for the current operation
  float SnapValue() const {
    switch (operation) {
      case ImGuizmo::ROTATE:
        return snap_rotate;
      case ImGuizmo::SCALE:
        return snap_scale;
      default:
        return snap_translate;
    }
  }

  static const char* OperationName(ImGuizmo::OPERATION op) {
    switch (op) {
      case ImGuizmo::TRANSLATE:
        return "Translate mode";
      case ImGuizmo::ROTATE:
        return "Rotate mode";
      case ImGuizmo::SCALE:
        return "Scale mode";
      case ImGuizmo::BOUNDS:
        return "Bounds mode";
      default:
        return "Transform mode";
    }
  }

  void HandleKeyboardShortcuts() {
    if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
      operation = ImGuizmo::TRANSLATE;
      Viewport::PostAction(OperationName(operation), "W");
    }
    if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
      operation = ImGuizmo::ROTATE;
      Viewport::PostAction(OperationName(operation), "E");
    }
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
      operation = ImGuizmo::SCALE;
      Viewport::PostAction(OperationName(operation), "R");
    }
    if (ImGui::IsKeyChordPressed(ImGuiKey_F)) {
      const Entity selected = Runtime::State().selected_entity;
      if (selected != entt::null &&
          ECS::Main().Has<TransformComponent>(selected)) {
        EditorEvents::focus_entity(selected);
        Viewport::PostAction(
            "Focus: " + ECS::Main().Get<TransformComponent>(selected).name_,
            "F");
      }
    }
  }
};

inline void GameCanvas(bool isGameRunning, bool& hovered, bool& focused,
                       const Viewport::ToolbarCallbacks& cb,
                       GodCameraState& camera_state) {
  static TransformGizmoState gizmo_state;

  ImGui::BeginChild("GameCanvas", ImVec2(0, ImGui::GetContentRegionAvail().y),
                    false);

  ImVec2 pos = ImGui::GetCursorScreenPos();  // Get current cursor position
  ImVec2 size = ImGui::GetContentRegionAvail();
  ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;

  // Always update position immediately
  const uint16_t rendered_w = canvasViewportW;
  const uint16_t rendered_h = canvasViewportH;
  const ImVec2 rendered_size(rendered_w / scale.x, rendered_h / scale.y);

  canvasViewportX = static_cast<uint16_t>(pos.x * scale.x);
  canvasViewportY = static_cast<uint16_t>(pos.y * scale.y);

  // Apply size immediately: the scene FBO is grow-only and rounded up to
  // 256px (GraphicsRenderer::PrepareFrame), so resizing rarely reallocates
  canvasViewportW = static_cast<uint16_t>(size.x * scale.x);
  canvasViewportH = static_cast<uint16_t>(size.y * scale.y);

  hovered = ImGui::IsWindowHovered();
  focused = ImGui::IsWindowFocused();

  auto* renderer = static_cast<GraphicsRenderer*>(Runtime::Renderer());

  // === 1. CAPTURE START POS FOR OVERLAYS ===
  // We capture this early so the toolbar can always anchor to the top of the
  // canvas even if the game image isn't currently being drawn.
  ImVec2 start_cursor_pos = ImGui::GetCursorPos();

  // === 2. DRAW GAME OR EMPTY STATE ===
  if (isGameRunning && canvasViewportW > 0 && canvasViewportH > 0) {

    // render when dimensions are valid
    ImTextureID texId = renderer->GetSceneTexId();
    if (texId) {

      // Rendered region is top-left sub-rect of FBO.
      const float u1 =
          std::min(1.0f, (float)rendered_w / renderer->GetFramebufferWidth());
      const float v1 =
          std::min(1.0f, (float)rendered_h / renderer->GetFramebufferHeight());

      // Draw scene image, round the corners that touch the panel edge
      ImGui::GetWindowDrawList()->AddImageRounded(
          texId, pos, ImVec2(pos.x + rendered_size.x, pos.y + rendered_size.y),
          ImVec2(0, v1), ImVec2(u1, 0), IM_COL32_WHITE,
          EditorSizes::panel_radius, ImDrawFlags_RoundCornersBottom);
      ImGui::Dummy(size);  // layout still uses the panel size

      // ImGuizmo transform manipulation
      auto& state = Runtime::State();
      auto& pipeline = Runtime::GetSceneViewPipeline();

      Entity selected_entity = state.selected_entity;
      bool has_selection = selected_entity != entt::null &&
                           ECS::Main().Has<TransformComponent>(selected_entity);

      // Get camera matrices
      auto& camera_transform = std::get<0>(pipeline.GetGodCamera());
      auto& camera_component = std::get<1>(pipeline.GetGodCamera());

      const glm::mat4& view = pipeline.GetView();
      const glm::mat4& projection = pipeline.GetProjection();

      /* Logger::getInstance().Log(
          LogLevel::Debug, "ImGuizmo view[0]: " + std::to_string(view[0][0]) +
                               ", " + std::to_string(view[0][1]) + ", " +
                               std::to_string(view[0][2])); */

      // Setup ImGuizmo viewport
      ImGuizmo::SetOrthographic(false);
      ImGuizmo::SetDrawlist();
      ImGuizmo::SetRect(pos.x, pos.y, rendered_size.x, rendered_size.y);

      // Build projection matrix for ImGuizmo
      float gizmoAspect = rendered_size.x / rendered_size.y;

      // Rebuild projection
      float tanHalfFov = glm::tan(glm::radians(camera_component.fov_) * 0.5f);
      glm::mat4 gizmoProjection(0.0f);
      gizmoProjection[0][0] = 1.0f / (gizmoAspect * tanHalfFov);
      gizmoProjection[1][1] = 1.0f / tanHalfFov;  // Y-flip for ImGuizmo
      gizmoProjection[2][2] =
          camera_component.far_plane_ /
          (camera_component.far_plane_ - camera_component.near_plane_);
      gizmoProjection[2][3] = 1.0f;
      gizmoProjection[3][2] =
          -(camera_component.near_plane_ * camera_component.far_plane_) /
          (camera_component.far_plane_ - camera_component.near_plane_);

      // Only show gizmos if: pipeline enabled, entity selected, not
      // interacting
      bool show_gizmos = pipeline.show_gizmos_ && has_selection;
      bool right_click = ImGui::IsMouseDown(ImGuiMouseButton_Right);
      bool middle_click = ImGui::IsMouseDown(ImGuiMouseButton_Middle);

      if (show_gizmos && !right_click && !middle_click) {
        auto& world = ECS::Main();
        if (has_selection) {

          // Handle keyboard shortcuts
          if (focused) {
            gizmo_state.HandleKeyboardShortcuts();
          }

          // Get transform
          auto& transform = world.Get<TransformComponent>(selected_entity);
          glm::mat4 modelMatrix = transform.model_;

          // Snapping: toolbar toggle
          const bool modifier = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) ||
                                ImGui::IsKeyDown(ImGuiKey_RightCtrl);
          bool snapping = gizmo_state.snap_enabled != modifier;
          float snapValue = gizmo_state.SnapValue();
          float snapValues[3] = {snapValue, snapValue, snapValue};

          // Manipulate
          bool manipulated = ImGuizmo::Manipulate(
              glm::value_ptr(view), glm::value_ptr(gizmoProjection),
              gizmo_state.operation, gizmo_state.mode,
              glm::value_ptr(modelMatrix),
              nullptr,  // delta
              snapping ? snapValues : nullptr);

          // If modified, update transform
          if (manipulated && ImGuizmo::IsUsing()) {
            glm::vec3 position, scale, skew;
            glm::vec4 perspective;
            glm::quat rotation;
            glm::decompose(modelMatrix, scale, rotation, position, skew,
                           perspective);

            Transform::SetPosition(transform, position, Space::WORLD);
            Transform::SetRotation(transform, glm::normalize(rotation),
                                   Space::WORLD);

            // Apply constraints for scale
            if (gizmo_state.operation == ImGuizmo::SCALE) {
              scale = glm::max(scale, glm::vec3(gizmo_state.scale_min));
            }

            Transform::SetScale(transform, scale, Space::WORLD);

            // Re-evaluate transform
            Transform::Evaluate(transform);
          }
        }
      }

      // Handle drop operations for glTF files after drawing image
      if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GLTF_FILE");
        if (payload) {
          // Get file path from payload
          std::string rel(static_cast<const char*>(payload->Data));

          // Convert to absolute path
          std::filesystem::path abs_path = Runtime::Project().AbsolutePath(rel);

          // Check extension before importing
          if (!FileSystem::HasExtension(abs_path, ".gltf") &&
              !FileSystem::HasExtension(abs_path, ".glb")) {
            Logger::getInstance().Log(
                LogLevel::Warning,
                "Dropped file is not a glTF model: " + abs_path.string());
          } else {
            Logger::getInstance().Log(
                LogLevel::Info, "Importing glTF model: " + abs_path.string());

            // Load meshes
            // ! Synchronous loading
            // TODO: replace implementation with LoadData() from model.h
            // instead
            std::shared_ptr<Model> model = Model::Load(abs_path.string());

            // debug
            Logger::getInstance().Log(
                LogLevel::Debug,
                "[GameCanvas] Model loaded, mesh count: " +
                    std::to_string(model ? model->NLoadedMeshes() : 0));

            if (!model) {
              Logger::getInstance().Log(
                  LogLevel::Error,
                  "Import failed: no meshes in " + abs_path.string());
            } else {
              // Create ECS entity
              auto& world = ECS::Main();
              auto [entity, tr] =
                  world.CreateEntity(abs_path.filename().string());

              // Get materials from loaded model
              const auto& materials = model->GetMaterials();

              // Attach a MeshRendererComponent for each mesh
              for (uint32_t i = 0; i < model->NLoadedMeshes(); ++i) {
                const Mesh* mesh = model->QueryMesh(i);
                if (!mesh)
                  continue;

                // Single mesh: keep it on the root so selecting the model
                // selects (and outlines) the mesh. Multiple meshes: one child
                // entity each, since an entity can only hold one MeshRenderer
                Entity mesh_entity = entity;
                if (model->NLoadedMeshes() > 1)
                  mesh_entity = std::get<0>(
                      world.CreateEntity("Mesh " + std::to_string(i), entity));

                MeshRendererComponent& mr =
                    world.Add<MeshRendererComponent>(mesh_entity);
                mr.mesh_ = mesh;
                mr.enabled_ = true;

                // Register material and assign handle
                uint32_t mat_idx = mesh->MaterialIndex();
                if (mat_idx < materials.size()) {
                  mr.material_ =
                      Renderer::MaterialRegistry::Instance().Register(
                          materials[mat_idx]);

                  Logger::getInstance().Log(
                      LogLevel::Debug, "[GameCanvas] Registered material '" +
                                           materials[mat_idx].name +
                                           "' with handle " +
                                           std::to_string(mr.material_));
                } else {
                  mr.material_ = Renderer::INVALID_MATERIAL;

                  Logger::getInstance().Log(
                      LogLevel::Warning,
                      "[GameCanvas] Mesh material_index " +
                          std::to_string(mat_idx) +
                          " out of range (materials.size()=" +
                          std::to_string(materials.size()) + ")");
                }
              }

              // Keep model alive for the lifetime of the scene
              // ! Potential memory leak
              // ! loaded models should be stored in a scene-level asset
              // manager
              static std::vector<std::shared_ptr<Model>> asset_cache;
              asset_cache.emplace_back(std::move(model));
            }
          }
        }
        ImGui::EndDragDropTarget();
      }

    } else {
      ImGui::SetCursorPos(ImVec2(size.x * 0.5f - 65.0f, size.y * 0.5f));
      ImGui::TextColored(ImVec4(1, 0, 0, 1),
                         ICON_FA_TRIANGLE_EXCLAMATION " Texture ID invalid!");
    }
  } else {
    // Draw Dark Background for stopped state so the canvas isn't entirely
    // blank
    ImGui::GetWindowDrawList()->AddRectFilled(
        pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(20, 20, 20, 255));

    // Draw centered status text
    ImGui::SetCursorPos(ImVec2(size.x * 0.5f - 70.0f, size.y * 0.5f));
    if (isGameRunning) {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                         ICON_FA_HOURGLASS_HALF " Resizing viewport...");
    } else {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                         ICON_FA_PLAY " Game not started");
    }
  }

  // === OVERLAYS ===
  ImVec2 overlay_padding =
      ImVec2(EditorSizes::overlay_margin, EditorSizes::overlay_margin);
  float overlay_y = start_cursor_pos.y + overlay_padding.y;

  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, EditorSizes::overlay_radius);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 6.0f));

  // Section background: panel color, translucent so the scene shows through
  const ImVec4 bg_pill =
      ImGui::ColorConvertU32ToFloat4(EditorColor::overlay_bg);
  const ImVec4 bg_pill_hover =
      ImGui::ColorConvertU32ToFloat4(EditorColor::overlay_hover);

  // Tools we only want visible when playing
  if (isGameRunning) {
    // --- Left Tools: Perspective / Lit (Individual Pills) ---
    ImGui::SetCursorPos(
        ImVec2(start_cursor_pos.x + overlay_padding.x, overlay_y));
    ImGui::BeginGroup();

    ImGui::PushStyleColor(ImGuiCol_Button, bg_pill);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bg_pill_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, bg_pill);

    ImGui::PushFont(EditorStyles::GetFonts().toolbar);
    if (IMComponents::IconLabelButton(ICON_FA_VIDEO, "Perspective")) {}
    ImGui::SameLine(0, 8.0f);
    if (IMComponents::IconLabelButton(ICON_FA_EYE, "Lit")) {}
    ImGui::PopFont();

    ImGui::PopStyleColor(3);
    ImGui::EndGroup();

    // --- Right Tools: Gizmos ---
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,
                        EditorSizes::overlay_radius);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
    ImGui::PushFont(EditorStyles::GetFonts().toolbar);

    const float btn_w = ImGui::GetFrameHeight() + 4.0f;  // near-square buttons
    static float gizmo_width = 0.0f;
    ImGui::SetCursorPos(
        ImVec2(start_cursor_pos.x + size.x - gizmo_width - overlay_padding.x,
               overlay_y));

    IMComponents::BeginOverlay();

    // Transparent buttons; the section draws the background
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));

    auto GizmoBtn = [&](const char* icon, ImGuizmo::OPERATION op) {
      if (IMComponents::ToolButton(icon, gizmo_state.operation == op,
                                   ImVec2(btn_w, 0))) {
        gizmo_state.operation = op;
        Viewport::PostAction(TransformGizmoState::OperationName(op));
      }
    };

    GizmoBtn(ICON_FA_ARROW_POINTER, ImGuizmo::BOUNDS);
    ImGui::SameLine();
    GizmoBtn(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, ImGuizmo::TRANSLATE);
    ImGui::SameLine();
    GizmoBtn(ICON_FA_ROTATE, ImGuizmo::ROTATE);
    ImGui::SameLine();
    GizmoBtn(ICON_FA_MAXIMIZE, ImGuizmo::SCALE);

    // Snap toggle
    ImGui::SameLine();
    IMComponents::VerticalDivider();
    ImGui::SameLine();
    {
      const bool on = gizmo_state.snap_enabled;
      if (IMComponents::ToolButton(ICON_FA_MAGNET, on, ImVec2(btn_w, 0))) {
        gizmo_state.snap_enabled = !on;
        Viewport::PostAction(gizmo_state.snap_enabled ? "Snapping on"
                                                      : "Snapping off");
      }
      IMComponents::Tooltip(on ? "Snapping on (hold Ctrl to move freely)"
                               : "Snapping off (hold Ctrl to snap)");
    }

    ImGui::PopStyleColor(3);
    IMComponents::EndOverlay();
    gizmo_width = ImGui::GetItemRectSize().x;

    ImGui::PopFont();
    ImGui::PopStyleVar(2);

    // --- View axis gizmo: right-aligned ---
    const float axis_radius = 34.0f;
    const float axis_extent = axis_radius + std::floor(axis_radius * 0.24f);
    const ImVec2 axis_center =
        ImVec2(pos.x + size.x - overlay_padding.x - axis_extent,
               pos.y + overlay_padding.y + ImGui::GetFrameHeight() + 12.0f +
                   axis_extent);
    auto& scene_pipeline = Runtime::GetSceneViewPipeline();
    Viewport::AxisGizmo(axis_center, axis_radius, scene_pipeline.GetView(),
                        camera_state,
                        std::get<0>(scene_pipeline.GetGodCamera()));
  }

  // --- Center Tools: Play / Stop (Toolbar) ---
  static float toolbar_width = 0.0f;
  ImGui::SetCursorPos(
      ImVec2(start_cursor_pos.x + (size.x - toolbar_width) * 0.5f, overlay_y));
  Viewport::Toolbar(cb);
  toolbar_width = ImGui::GetItemRectSize().x;

  // --- Stats (bottom-right) ---
  Viewport::StatsOverlay(pos, size, camera_state.move_speed);

  // --- Action feed (bottom-left) ---
  Viewport::ActionOverlay(pos, size);

  ImGui::PopStyleVar(3);

  ImGui::EndChild();
}

}  // namespace Panels

#endif  // GAME_CANVAS_H