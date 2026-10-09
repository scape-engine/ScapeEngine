#include "inspectable_components.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

#include <cstdint>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <unordered_map>

#include "editor/editor_ui.h"
#include "editor/events.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"

#include "im_components.h"

#include "engine/context/engine_context.h"  // ! should be removed?
#include "engine/pcg/generator_resource.h"  // ! should be removed?
#include "engine/transform/transform.h"
// TODO: #include "rendering/icons/icon_pool.h"

namespace InspectableComponents {

//=============================================================================
// INTERNAL STATE
//=============================================================================

// Store collapsed state for each component
std::unordered_map<uint32_t, bool> g_opened;

// Top-left of the component currently being drawn
ImVec2 g_component_top;

// Return pointer to opened state
bool* _IsOpened(uint32_t component_id) {
  return &g_opened[component_id];
}

// Remove ECS component from entity
template <typename T>
void _EcsRemove(Entity entity) {
  ECS::Main().Remove<T>(entity);
}

//=============================================================================
// COMPONENT HEADER DRAWING
//=============================================================================

/**
 *
 * Draw a component header, return true if expanded
 * - Set enabledPtr to nullptr if component can't be disabled
 * - Set removedPtr to nullptr if component can't be removed
 * - Set alwaysOpened to true for components that can't be collapsed
 *
 */
bool _BeginComponent(const std::string& identifier,
                     uint32_t icon,  // TODO: Use IconPool texture ID
                     bool* enabled_ptr = nullptr, bool* removed_ptr = nullptr,
                     bool always_opened = false) {
  ImDrawList& draw_list = *ImGui::GetWindowDrawList();

  const float header_height = EditorSizes::header_bar_height;
  const float pad_x = 10.0f;
  const float radius = 6.0f;
  ImFont* small = EditorStyles::GetFonts().s;

  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 p1 =
      ImVec2(p0.x + ImGui::GetContentRegionAvail().x, p0.y + header_height);

  uint32_t component_id = entt::hashed_string::value(identifier.c_str());
  bool* opened_ptr = _IsOpened(component_id);
  const bool opened = always_opened || *opened_ptr;

  // HEADER BACKGROUND (only top corners rounded while open)
  draw_list.AddRectFilled(
      p0, p1, EditorColor::section_header, radius,
      opened ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll);
  if (!opened)
    draw_list.AddRect(p0, p1, EditorColor::panel_stroke, radius, 0, 1.0f);

  // CHEVRON
  float x = p0.x + pad_x;
  IMComponents::Glyph(
      draw_list, ImVec2(x, p0.y), ImVec2(12.0f, header_height),
      opened ? ICON_FA_CHEVRON_DOWN : ICON_FA_CHEVRON_RIGHT,
      always_opened ? EditorColor::text_disabled : EditorColor::chevron, small);
  x += 12.0f + 8.0f;

  // ENABLED CHECKBOX
  if (enabled_ptr) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
    const float box = ImGui::GetFrameHeight();
    const ImVec2 restore = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(x, p0.y + (header_height - box) * 0.5f));
    IMComponents::Checkbox(("##enabled_" + identifier).c_str(), enabled_ptr);
    const float checkbox_w = ImGui::GetItemRectSize().x;
    ImGui::SetCursorScreenPos(restore);
    ImGui::PopStyleVar();
    x += checkbox_w + 6.0f;
  }

  // TITLE
  ImFont* font = EditorStyles::GetFonts().p;
  draw_list.AddText(font, font->FontSize,
                    ImVec2(x, p0.y + (header_height - font->FontSize) * 0.5f),
                    EditorColor::text, identifier.c_str());

  // GRIP (2x4 dots, placeholder for drag-reordering)
  float right = p1.x - pad_x;
  const float dot = 1.0f;
  const float step = 4.0f;
  for (int col = 0; col < 4; ++col)
    for (int row = 0; row < 2; ++row)
      draw_list.AddCircleFilled(
          ImVec2(right - dot - col * step,
                 p0.y + header_height * 0.5f + (row - 0.5f) * step),
          dot, EditorColor::text_disabled);
  right -= 3.0f * step + 2.0f * dot + 8.0f;

  // REMOVE BUTTON
  bool remove_clicked = false;
  if (removed_ptr) {
    const ImVec2 x_min = ImVec2(right - 16.0f, p0.y);
    const bool x_hovered =
        ImGui::IsMouseHoveringRect(x_min, ImVec2(right, p1.y));
    IMComponents::Glyph(
        draw_list, x_min, ImVec2(16.0f, header_height), ICON_FA_XMARK,
        x_hovered ? EditorColor::error : EditorColor::text_disabled, small);
    if (x_hovered && ImGui::IsMouseClicked(0)) {
      *removed_ptr = true;
      remove_clicked = true;
    }
  }

  // TOGGLE (left click anywhere on the header except the controls)
  if (!always_opened && ImGui::IsMouseHoveringRect(p0, p1) &&
      ImGui::IsMouseClicked(0) && !remove_clicked && !ImGui::IsAnyItemHovered())
    *opened_ptr = !*opened_ptr;

  ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y));
  if (!opened) {
    ImGui::Dummy(ImVec2(0.0f, 4.0f));  // gap to next component
    return false;
  }

  // BODY: auto-height child gives padding on both sides
  g_component_top = p0;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad_x, 8.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
  ImGui::BeginChild(
      ("##body_" + identifier).c_str(), ImVec2(0.0f, 0.0f),
      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);

  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
  return true;
}

void _EndComponent() {
  ImGui::EndChild();

  // Drawn on the parent list, which renders before the child: ends up behind
  ImDrawList& draw_list = *ImGui::GetWindowDrawList();
  const float radius = 6.0f;
  const ImVec2 body_min = ImGui::GetItemRectMin();
  const ImVec2 body_max = ImGui::GetItemRectMax();
  draw_list.AddRectFilled(body_min, body_max, EditorColor::panel, radius,
                          ImDrawFlags_RoundCornersBottom);
  draw_list.AddRect(ImVec2(body_min.x, g_component_top.y), body_max,
                    EditorColor::panel_stroke, radius, 0, 1.0f);

  ImGui::Dummy(ImVec2(0.0f, 4.0f));  // gap to next component
}

//=============================================================================
// HELPER WIDGETS
//=============================================================================
void _Headline(const std::string& label) {
  IMComponents::Label(label, EditorStyles::GetFonts().h4_bold);
  ImGui::Dummy(ImVec2(0.0f, 1.0f));
}

void _SpacingS() {
  ImGui::Dummy(ImVec2(0.0f, 3.0f));
}

void _SpacingM() {
  ImGui::Dummy(ImVec2(0.0f, 10.0f));
}

//=============================================================================
// COMPONENT INSPECTABLES
//=============================================================================
void DrawTransform(Entity entity, TransformComponent& transform) {
  // TODO: Use IconPool here, IconPool::get("transform")
  if (_BeginComponent("Transform", 0, nullptr, nullptr, true)) {
    _Headline("Properties");

    IMComponents::Input("Position", transform.position_);
    IMComponents::Input("Rotation", transform.euler_angles_);
    IMComponents::Input("Scale", transform.scale_);

    if (Transform::HasParent(transform)) {
      auto& parent = Transform::GetParent(transform);
      IMComponents::Label("Parent: " + parent.name_);
    }

    IMComponents::Label("ID: " + std::to_string(transform.id_));
    IMComponents::Label("Depth: " + std::to_string(transform.depth_));

    if (Transform::HasParent(transform)) {
      IMComponents::VectorLabel(
          "World Position", Transform::GetPosition(transform, Space::WORLD));
      IMComponents::VectorLabel("World Scale",
                                Transform::GetScale(transform, Space::WORLD));
    }

    // Apply changes
    Transform::SetPosition(transform, transform.position_);
    Transform::SetEulerAngles(transform, transform.euler_angles_);
    Transform::SetScale(transform, transform.scale_);

    _EndComponent();
  }
}

void DrawMeshRenderer(Entity entity, MeshRendererComponent& mesh_renderer) {
  bool removed = false;

  // TODO: Use IconPool here, IconPool::Get("mesh_renderer")
  if (_BeginComponent("Mesh Renderer", 0, &mesh_renderer.enabled_, &removed)) {
    _Headline("General");

    // TODO: meshRenderer.mesh->vao() instead of IndexCount
    if (mesh_renderer.mesh_) {
      IMComponents::Label("Index Count: " +
                          std::to_string(mesh_renderer.mesh_->IndexCount()));
    } else {
      ImGui::TextDisabled("No mesh assigned");
    }

    // TODO: Use Material display instead ?
    // if (renderer.material_) {
    //   _Label("Material ID: " + std::to_string(renderer.material_->GetId()));
    // }

    _EndComponent();
  }

  if (removed)
    _EcsRemove<MeshRendererComponent>(entity);
}

void DrawCamera(Entity entity, CameraComponent& camera) {
  bool removed = false;

  // TODO: Do IconPool::Get("camera")
  if (_BeginComponent("Camera", 0, &camera.enabled_, &removed)) {
    _Headline("General");

    IMComponents::Input("FOV", camera.fov_);
    IMComponents::Input("Near", camera.near_plane_);
    IMComponents::Input("Far", camera.far_plane_);

    _EndComponent();
  }

  if (removed)
    _EcsRemove<CameraComponent>(entity);
}

void DrawDirectionalLightComponent(
    Entity entity, DirectionalLightComponent& directional_light) {
  bool removed = false;

  // TODO: Use IconPool here, IconPool::Get("directional_light")
  if (_BeginComponent("Directional Light", 0, &directional_light.enabled_,
                      &removed)) {
    _Headline("Properties");

    IMComponents::Input("Intensity", directional_light.intensity_);
    IMComponents::ColorPicker("Color", directional_light.color_);

    _SpacingS();
    _Headline("Shadows");

    // TODO: Shadow settings
    bool tmp_cast = true;
    bool tmp_soft = true;
    IMComponents::Input("Cast Shadows", tmp_cast);
    IMComponents::Input("Soft Shadows", tmp_soft);

    IMComponents::Label(
        ICON_FA_TRIANGLE_EXCLAMATION
        " In-editor shadows aren't dynamic yet and can't be changed currently",
        EditorStyles::GetFonts().p, IM_COL32(255, 255, 0, 135));

    _EndComponent();
  }

  if (removed)
    _EcsRemove<DirectionalLightComponent>(entity);
}

void DrawPointLightComponent(Entity entity, PointLightComponent& point_light) {
  bool removed = false;

  // TODO: Replace ICON_FA_LIGHTBULB with IconPool::Get("point_light")
  if (_BeginComponent("Point Light", 0, &point_light.enabled_, &removed)) {
    _Headline("Properties");

    IMComponents::Input("Intensity", point_light.intensity_);
    IMComponents::ColorPicker("Color", point_light.color_);
    IMComponents::Input("Range", point_light.range_);
    IMComponents::Input("Falloff", point_light.falloff_);

    _SpacingS();
    _Headline("Shadows");

    bool tmp_cast = false;
    bool tmp_soft = false;
    IMComponents::Input("Cast Shadows", tmp_cast);
    IMComponents::Input("Soft Shadows", tmp_soft);

    IMComponents::Label(
        ICON_FA_TRIANGLE_EXCLAMATION
        " In-editor shadows aren't dynamic yet and can't be changed currently",
        EditorStyles::GetFonts().p, IM_COL32(255, 255, 0, 135));

    _EndComponent();
  }

  if (removed)
    _EcsRemove<PointLightComponent>(entity);
}

void DrawSpotlightComponent(Entity entity, SpotlightComponent& spotlight) {
  bool removed = false;

  // TODO: Use IconPool::Get("spotlight")
  if (_BeginComponent("Spotlight", 0, &spotlight.enabled_, &removed)) {
    _Headline("Properties");

    IMComponents::Input("Intensity", spotlight.intensity_);
    IMComponents::ColorPicker("Color", spotlight.color_);
    IMComponents::Input("Range", spotlight.range_);
    IMComponents::Input("Falloff", spotlight.falloff_);
    IMComponents::Input("Inner Angle", spotlight.inner_angle_);
    IMComponents::Input("Outer Angle", spotlight.outer_angle_);

    _SpacingS();
    _Headline("Shadows");

    bool tmp_cast = true;
    bool tmp_soft = true;
    IMComponents::Input("Cast Shadows", tmp_cast);
    IMComponents::Input("Soft Shadows", tmp_soft);

    IMComponents::Label(
        ICON_FA_TRIANGLE_EXCLAMATION
        " In-editor shadows aren't dynamic yet and can't be changed currently",
        EditorStyles::GetFonts().p, IM_COL32(255, 255, 0, 135));

    _EndComponent();
  }

  if (removed)
    _EcsRemove<SpotlightComponent>(entity);
}

void DrawVolumeComponent(Entity entity, VolumeComponent& volume) {
  bool removed = false;

  if (_BeginComponent("Volume", 0, nullptr, &removed)) {
    _Headline("Properties");

    int32_t res = static_cast<int32_t>(volume.resolution);
    IMComponents::Input("Resolution", res, 1.0f);
    if (res != static_cast<int32_t>(volume.resolution)) {
      volume.resolution = static_cast<uint16_t>(glm::clamp(res, 64, 4096));
      volume.dirty = true;
    }

    _SpacingS();
    _Headline("Generator");

    if (volume.generator_id != 0) {
      // Show ID and button to open the graph editor
      IMComponents::Label("Generator ID: " +
                          std::to_string(volume.generator_id));

      if (ImGui::Button("Open Graph Editor")) {
        EditorEvents::open_graph_editor(volume.generator_id);
      }
    } else {
      ImGui::TextDisabled("No generator selected");

      if (ImGui::Button("Create PCG Graph")) {
        auto& res_mgr = EngineContext::resourceManager();
        auto [id, resource] =
            res_mgr.Create<PCG::GeneratorResource>("PCG Graph");
        resource->SetGenerator(PCG::CreateGenerator(PCG::GeneratorType::Graph));

        volume.generator_id = id;
        volume.dirty = true;

        EditorEvents::open_graph_editor(id);
      }
    }

    _EndComponent();
  }

  if (removed)
    _EcsRemove<VolumeComponent>(entity);
}

// TODO: component inspectables
// void DrawVelocityBlur(Entity entity, VelocityBlurComponent& velocity);
// void DrawBoxCollider(Entity entity, BoxColliderComponent& collider);
// void DrawSphereCollider(Entity entity, SphereColliderComponent&
// collider); void DrawRigidbody(Entity entity, RigidbodyComponent&
// rigidbody); void DrawAudioListener(Entity entity, AudioListenerComponent&
// listener); void DrawAudioSource(Entity entity, AudioSourceComponent&
// source);
// some PCG component? (generator, instancing, etc)

// TODO: Post-processing inspectables
// void DrawColor(PostProcessing::Color& color);
// void DrawMotionBlur(PostProcessing::MotionBlur& motionBlur);
// void DrawBloom(PostProcessing::Bloom& bloom);
// void DrawChromaticAberration(PostProcessing::ChromaticAberration& ca);
// void DrawVignette(PostProcessing::Vignette& vignette);
// void DrawAmbientOcclusion(PostProcessing::AmbientOcclusion& ao);

}  // namespace InspectableComponents