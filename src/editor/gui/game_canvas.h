#ifndef GAME_CANVAS_H
#define GAME_CANVAS_H

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <imgui.h>
#include <algorithm>

#include "editor/gui/components/inspectable_components.h"
#include "editor/gui/components/instance_edits.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/gui/utils/asset_drag.h"
#include "editor/gui/viewport/action_overlay.h"
#include "editor/gui/viewport/axis_gizmo.h"
#include "editor/gui/viewport/camera_menu.h"
#include "editor/gui/viewport/gizmos_menu.h"
#include "editor/gui/viewport/grid_menu.h"
#include "editor/gui/viewport/stats_overlay.h"
#include "editor/gui/viewport/toolbar.h"
#include "editor/gui/viewport/viewport_settings.h"
#include "editor/gui/viewport/views_menu.h"
#include "editor/runtime/runtime.h"

#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"
#include "editor/vendor/imguizmo/ImGuizmo.h"

#include "engine/core/engine_globals.h"
#include "engine/core/logger.h"
#include "engine/core/types/material_data.h"
#include "engine/core/view_ids.h"

#include "engine/ecs/components/instance_source_component.h"
#include "engine/ecs/world.h"
#include "engine/math/transformation.h"

#include "engine/renderer/frame_graph.h"
#include "engine/renderer/graphics_renderer.h"
#include "engine/renderer/material/material_registry.h"
#include "engine/scene/model_entity_factory.h"

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
                       GodCameraState& camera_state,
                       Viewport::ViewportSettings& view_settings) {
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
      // An instance moves with its set, and a generated entity is set again
      // by its generator: no gizmo for them
      bool has_selection =
          selected_entity != entt::null && state.selected_instance < 0 &&
          ECS::Main().Has<TransformComponent>(selected_entity) &&
          InspectableComponents::FindOrigin(selected_entity).volume ==
              entt::null;

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
            Runtime::Project().MarkSceneDirty();
          }
        }
      }

      // A click (not a drag, nor on a gizmo or a button) selects what shows
      // under it, once the pick comes back. While an .object asset is dragged
      // over it, picks find where it would stand instead
      static bool pressed_here = false;
      static bool drop_picking = false;  // the pick under way is a drop's
      static bool drop_pending = false;
      static std::optional<PickPass::Hit> drop_hit;
      if (!ImGui::GetDragDropPayload())
        drop_hit.reset();
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        pressed_here = hovered && !ImGui::IsAnyItemHovered() &&
                       !ImGuizmo::IsOver() && !ImGuizmo::IsUsing();
      if (pressed_here && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        pressed_here = false;
        const ImVec2 at = ImGui::GetMousePos() - pos;
        const ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        if (drag.x * drag.x + drag.y * drag.y <
                ImGui::GetIO().MouseDragThreshold *
                    ImGui::GetIO().MouseDragThreshold &&
            at.x >= 0.0f && at.y >= 0.0f && at.x < rendered_size.x &&
            at.y < rendered_size.y) {
          // The image shows the scene texture upside down
          drop_picking = false;
          pipeline.Pick(glm::vec2(at.x * scale.x,
                                  rendered_h - 1.0f - at.y * scale.y));
        }
      }
      if (const auto hit = pipeline.TakePick()) {
        if (drop_picking) {
          drop_pending = false;
          drop_hit = hit;
        } else if (hit->entity == entt::null ||
                   !ECS::Main().Reg().valid(hit->entity))
          state.ClearSelection();
        else if (hit->instance >= 0)
          state.SelectInstance(hit->entity, hit->instance);
        else {
          // A part of an object made as an entity selects the object
          auto& ecs = ECS::Main();
          Entity entity = hit->entity;
          for (Entity up = entity; ecs.Reg().valid(up);
               up = ecs.Get<TransformComponent>(up).parent_)
            if (ecs.Has<InstanceSourceComponent>(up)) {
              entity = up;
              break;
            }
          state.SelectEntity(entity);
        }
      }

      // Handle drop operations for glTF files after drawing image
      if (ImGui::BeginDragDropTarget()) {
        // An .object asset stands on a planet where it's dropped: a marker
        // shows where, on the planet's surface under the cursor
        // TODO: show the object itself while it is dragged
        if (const ImGuiPayload* payload = AssetDrag::Accept(
                {".object"}, ImGuiDragDropFlags_AcceptBeforeDelivery |
                                 ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
          if (!drop_pending || !drop_picking) {
            const ImVec2 at = ImGui::GetMousePos() - pos;
            drop_picking = drop_pending = true;
            pipeline.Pick(glm::vec2(at.x * scale.x,
                                    rendered_h - 1.0f - at.y * scale.y));
          }
          const Entity volume =
              drop_hit && ECS::Main().Reg().valid(drop_hit->entity)
                  ? InspectableComponents::FindOrigin(drop_hit->entity).volume
                  : entt::null;
          if (volume != entt::null) {
            const auto screen = [&](const glm::vec3& point) {
              const glm::vec4 clip =
                  gizmoProjection * view * glm::vec4(point, 1.0f);
              return ImVec2(
                  pos.x + (clip.x / clip.w * 0.5f + 0.5f) * rendered_size.x,
                  pos.y + (0.5f - clip.y / clip.w * 0.5f) * rendered_size.y);
            };
            const float reach =
                0.05f * glm::distance(glm::vec3(glm::inverse(view)[3]),
                                      drop_hit->position);
            auto* draw = ImGui::GetWindowDrawList();
            const ImVec2 base = screen(drop_hit->position);
            draw->AddLine(base,
                          screen(drop_hit->position + drop_hit->normal * reach),
                          EditorColor::accent, 2.0f);
            draw->AddCircleFilled(base, 4.0f, EditorColor::accent);
          }
          if (payload->IsDelivery()) {
            const std::string object = AssetDrag::Path(*payload);
            if (volume == entt::null ||
                !InstanceEdits::Place(volume, object, drop_hit->position))
              Logger::getInstance().Log(
                  LogLevel::Warning,
                  "Drop " + object + " onto a planet's terrain to place it");
            drop_hit.reset();
          }
        }

        if (const ImGuiPayload* payload =
                AssetDrag::Accept({".gltf", ".glb"})) {
          const std::string model = AssetDrag::Path(*payload);
          Logger::getInstance().Log(LogLevel::Info,
                                    "Importing glTF model: " + model);
          const Entity entity = ModelEntityFactory::InstanceModel(
              ECS::Main(), model, Runtime::Project().GetProject().path_);
          if (entity != entt::null)
            Runtime::Project().MarkSceneDirty();
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

  // Tools we only want visible when playing
  if (isGameRunning) {
    // --- Left Tools: transform tools + snapping ---
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,
                        EditorSizes::overlay_radius);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
    ImGui::PushFont(EditorStyles::GetFonts().toolbar);

    const float btn_w = ImGui::GetFrameHeight() + 4.0f;  // near-square buttons
    ImGui::SetCursorPos(
        ImVec2(start_cursor_pos.x + overlay_padding.x, overlay_y));

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

    ImGui::PopFont();
    ImGui::PopStyleVar(2);

    auto& scene_pipeline = Runtime::GetSceneViewPipeline();

    // --- Right Tools: view options ---
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,
                        EditorSizes::overlay_radius);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
    ImGui::PushFont(EditorStyles::GetFonts().toolbar);

    static float view_options_width = 0.0f;  // measured last frame
    ImGui::SetCursorPos(ImVec2(
        start_cursor_pos.x + size.x - view_options_width - overlay_padding.x,
        overlay_y));

    IMComponents::BeginOverlay();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));

    IMComponents::SplitToolButton(ICON_FA_LAYER_GROUP, nullptr, "##ViewsMenu",
                                  ImVec2(btn_w, 0), "Views and overlays");
    ImGui::SameLine();
    if (IMComponents::SplitToolButton(
            ICON_FA_VECTOR_SQUARE, &view_settings.gizmos.enabled,
            "##GizmosMenu", ImVec2(btn_w, 0), "Gizmos"))
      Viewport::PostAction(view_settings.gizmos.enabled ? "Gizmos on"
                                                        : "Gizmos off");
    ImGui::SameLine();
    if (IMComponents::SplitToolButton(ICON_FA_TABLE_CELLS,
                                      &view_settings.grid.enabled, "##GridMenu",
                                      ImVec2(btn_w, 0), "Grid"))
      Viewport::PostAction(view_settings.grid.enabled ? "Grid on" : "Grid off");
    ImGui::SameLine();
    IMComponents::SplitToolButton(ICON_FA_CAMERA, nullptr, "##CameraMenu",
                                  ImVec2(btn_w, 0), "Camera");

    ImGui::PopStyleColor(3);
    IMComponents::EndOverlay();
    view_options_width = ImGui::GetItemRectSize().x;

    // Viewport menu contents
    if (IMComponents::BeginToolPopup("##ViewsMenu")) {
      Viewport::ViewsMenu(view_settings);
      IMComponents::EndToolPopup();
    }
    if (IMComponents::BeginToolPopup("##GizmosMenu")) {
      Viewport::GizmosMenu(view_settings.gizmos);
      IMComponents::EndToolPopup();
    }
    if (IMComponents::BeginToolPopup("##GridMenu")) {
      Viewport::GridMenu(view_settings.grid);
      IMComponents::EndToolPopup();
    }
    if (IMComponents::BeginToolPopup("##CameraMenu")) {
      Viewport::CameraMenu(camera_state,
                           std::get<0>(scene_pipeline.GetGodCamera()),
                           std::get<1>(scene_pipeline.GetGodCamera()));
      IMComponents::EndToolPopup();
    }

    ImGui::PopFont();
    ImGui::PopStyleVar(2);

    // --- View axis gizmo: right-aligned ---
    const float axis_radius = 34.0f;
    const float axis_extent = axis_radius + std::floor(axis_radius * 0.24f);
    const ImVec2 axis_center =
        ImVec2(pos.x + size.x - overlay_padding.x - axis_extent,
               pos.y + overlay_padding.y + ImGui::GetFrameHeight() + 12.0f +
                   axis_extent);
    if (view_settings.overlays.axis_gizmo)
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
  Viewport::StatsOverlay(pos, size, camera_state.move_speed,
                         view_settings.overlays.stats);

  // --- Action feed (bottom-left) ---
  if (view_settings.overlays.notifications)
    Viewport::ActionOverlay(pos, size);

  // --- View settings feed ---
  Viewport::ApplySettings(view_settings, Runtime::GetSceneViewPipeline());

  ImGui::PopStyleVar(3);

  ImGui::EndChild();
}

}  // namespace Panels

#endif  // GAME_CANVAS_H