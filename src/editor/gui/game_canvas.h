#ifndef GAME_CANVAS_H
#define GAME_CANVAS_H

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <imgui.h>

#include "editor/gui/toolbar.h"
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

  void HandleKeyboardShortcuts() {
    if (ImGui::IsKeyPressed(ImGuiKey_W))
      operation = ImGuizmo::TRANSLATE;
    if (ImGui::IsKeyPressed(ImGuiKey_E))
      operation = ImGuizmo::ROTATE;
    if (ImGui::IsKeyPressed(ImGuiKey_R))
      operation = ImGuizmo::SCALE;
  }
};

// calculate squared distance between two vectors
// Avoids rapid FBO recreation while resizing scene window
inline float DistanceSqr(const ImVec2& delta) {
  return delta.x * delta.x + delta.y * delta.y;
}

// Resize is deferred until the viewport has stabilized
inline void GameCanvas(bool isGameRunning, bool& hovered, bool& focused,
                       const ToolbarCallbacks& cb) {
  static TransformGizmoState gizmo_state;

  ImGui::BeginChild("GameCanvas", ImVec2(0, ImGui::GetContentRegionAvail().y),
                    true);

  ImVec2 pos = ImGui::GetCursorScreenPos();  // Get current cursor position
  ImVec2 size = ImGui::GetContentRegionAvail();
  ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;

  // Always update position immediately
  canvasViewportX = static_cast<uint16_t>(pos.x * scale.x);
  canvasViewportY = static_cast<uint16_t>(pos.y * scale.y);

  // Store raw size before applying
  ImVec2 proposedSize = size;
  ImVec2 scaledSize =
      ImVec2(proposedSize.x * scale.x, proposedSize.y * scale.y);

  // Update if dimensions have stabilized
  static ImVec2 lastSize = ImVec2(0, 0);
  static int stableFrames = 0;

  if (DistanceSqr(ImVec2(scaledSize.x - lastSize.x,
                         scaledSize.y - lastSize.y)) < 4.0f) {
    stableFrames++;
  } else {
    stableFrames = 0;
  }
  lastSize = scaledSize;

  if (stableFrames >= 2) {
    canvasViewportW = static_cast<uint16_t>(scaledSize.x);
    canvasViewportH = static_cast<uint16_t>(scaledSize.y);
  }

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

      // Draw scene image
      ImGui::Image(texId, size, ImVec2(0, 1), ImVec2(1, 0));

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
      ImGuizmo::SetRect(pos.x, pos.y, size.x, size.y);

      // Build projection matrix for ImGuizmo
      float gizmoAspect = size.x / size.y;

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

      // Only show gizmos if: pipeline enabled, entity selected, not interacting
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

          // Snapping (hold Ctrl)
          bool snapping = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) ||
                          ImGui::IsKeyDown(ImGuiKey_RightCtrl);
          float snapValue =
              (gizmo_state.operation == ImGuizmo::ROTATE) ? 45.0f : 0.5f;
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
            // TODO: replace implementation with LoadData() from model.h instead
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

                MeshRendererComponent& mr =
                    world.Add<MeshRendererComponent>(entity);
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
    // Draw Dark Background for stopped state so the canvas isn't entirely blank
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

  // === 3. UE5 STYLE OVERLAYS (ALWAYS DRAWN) ===
  // These are drawn at the end so they overlay both the rendered game image
  // AND the dark background when the game is stopped.
  ImVec2 overlay_padding = ImVec2(15.0f, 15.0f);
  float overlay_y = start_cursor_pos.y + overlay_padding.y;

  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 6.0f));

  // Pull dynamically from EditorStyles
  ImVec4 bg_pill =
      ImGui::GetStyle().Colors[ImGuiCol_PopupBg];  // The #212121 color
  bg_pill.w = 0.90f;                               // Add slight transparency

  ImVec4 bg_pill_hover = ImGui::GetStyle().Colors[ImGuiCol_HeaderHovered];
  ImVec4 active_accent =
      ImGui::GetStyle().Colors[ImGuiCol_HeaderActive];  // The Slate Blue

  // Tools we only want visible when playing
  if (isGameRunning) {
    // --- Left Tools: Perspective / Lit (Individual Pills) ---
    ImGui::SetCursorPos(
        ImVec2(start_cursor_pos.x + overlay_padding.x, overlay_y));
    ImGui::BeginGroup();

    ImGui::PushStyleColor(ImGuiCol_Button, bg_pill);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bg_pill_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, bg_pill);

    if (ImGui::Button(ICON_FA_VIDEO " Perspective")) {}
    ImGui::SameLine(0, 8.0f);  // 8px gap between pills
    if (ImGui::Button(ICON_FA_EYE " Lit")) {}

    ImGui::PopStyleColor(3);
    ImGui::EndGroup();

    // --- Right Tools: Gizmos (Connected Pill) ---
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));

    // Calculate dimensions for the background pill
    float btn_w = ImGui::CalcTextSize(ICON_FA_ARROW_POINTER).x + 24.0f;
    float gizmo_width = btn_w * 4.0f;
    ImVec2 gizmo_pos =
        ImVec2(start_cursor_pos.x + size.x - gizmo_width - overlay_padding.x,
               overlay_y);

    // Draw background pill manually behind the buttons
    ImVec2 screen_pos = ImVec2(pos.x + gizmo_pos.x, pos.y + gizmo_pos.y);
    ImGui::GetWindowDrawList()->AddRectFilled(
        screen_pos,
        ImVec2(screen_pos.x + gizmo_width,
               screen_pos.y + ImGui::GetFrameHeight()),
        ImGui::ColorConvertFloat4ToU32(bg_pill), 12.0f);

    ImGui::SetCursorPos(gizmo_pos);
    ImGui::BeginGroup();

    // Override button backgrounds to be completely transparent for this group
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));

    // Helper lambda to draw active state easily
    auto GizmoBtn = [&](const char* icon, ImGuizmo::OPERATION op) {
      bool is_active = (gizmo_state.operation == op);
      if (is_active)
        ImGui::PushStyleColor(ImGuiCol_Text, active_accent);

      if (ImGui::Button(icon, ImVec2(btn_w, 0)))
        gizmo_state.operation = op;

      if (is_active)
        ImGui::PopStyleColor();
    };

    GizmoBtn(ICON_FA_ARROW_POINTER, ImGuizmo::BOUNDS);
    ImGui::SameLine();
    GizmoBtn(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, ImGuizmo::TRANSLATE);
    ImGui::SameLine();
    GizmoBtn(ICON_FA_ROTATE, ImGuizmo::ROTATE);
    ImGui::SameLine();
    GizmoBtn(ICON_FA_MAXIMIZE, ImGuizmo::SCALE);

    ImGui::EndGroup();
    ImGui::PopStyleColor(3);  // pop transparent button colors
    ImGui::PopStyleVar();     // pop tight item spacing
  }

  // --- Center Tools: Play / Stop (Toolbar) ---
  // Drawn OUTSIDE the isGameRunning check so it's always persistent!
  float toolbar_width = 200.0f;
  ImGui::SetCursorPos(
      ImVec2(start_cursor_pos.x + (size.x - toolbar_width) * 0.5f, overlay_y));
  ImGui::BeginGroup();
  Panels::Toolbar(cb);
  ImGui::EndGroup();

  // Pop global styles
  ImGui::PopStyleVar(3);

  ImGui::EndChild();
}

}  // namespace Panels

#endif  // GAME_CANVAS_H