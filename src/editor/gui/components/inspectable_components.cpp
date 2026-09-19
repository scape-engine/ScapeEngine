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

  ImVec2 content_avail = ImGui::GetContentRegionAvail();
  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();

  float title_height = EditorSizes::h4_font_size;
  ImVec2 title_padding = ImVec2(10.0f, 10.0f);

  float height = title_height + title_padding.y * 2;
  float y_margin = 2.0f;
  ImVec2 size = ImVec2(content_avail.x, height);

  ImVec2 p0 = ImVec2(cursor_pos.x, cursor_pos.y + y_margin);
  ImVec2 p1 = ImVec2(p0.x + size.x, p0.y + size.y);
  const bool hovered = ImGui::IsMouseHoveringRect(p0, p1);

  ImVec2 cursor = p0 + title_padding;

  bool always_enabled = !enabled_ptr;

  uint32_t component_id = entt::hashed_string::value(identifier.c_str());
  bool* opened_ptr = _IsOpened(component_id);

  // Click to collapse
  if (!always_opened) {
    if (hovered && ImGui::IsMouseClicked(2))
      *opened_ptr = !(*opened_ptr);
  }

  // Draw background
  draw_list.AddRectFilled(p0, p1, IM_COL32(30, 30, 30, 255), 10.0f);

  // Draw expansion caret
  if (always_opened) {
    draw_list.AddText(EditorStyles::GetFonts().h4_bold,
                      EditorSizes::h4_font_size, cursor, EditorColor::text,
                      ICON_FA_CARET_DOWN);
  } else {
    IMComponents::Caret(*opened_ptr, draw_list, cursor, ImVec2(-1.0f, 2.0f),
                        IM_COL32(0, 0, 0, 0), EditorColor::element);
  }
  cursor.x += 22.0f;

  // Draw component icon
  // TODO: Use IconPool::Get(icon) texture
  // ImVec2 icon_size = ImVec2(18.0f, 18.0f);
  // draw_list.AddImage(icon, cursor, cursor + icon_size, ImVec2(0, 1),
  // ImVec2(1, 0)); cursor.x += icon_size.x + 8.0f;
  cursor.x += 8.0f;  // ! Placeholder spacing

  // Draw enabled checkbox
  if (!always_enabled) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3, 3));

    ImVec2 internal_cursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(cursor + ImVec2(0.0f, -2.0f));
    std::string id = EditorUI::Get()->GenerateIdString();
    ImGui::Checkbox(id.c_str(), enabled_ptr);
    ImGui::SetCursorScreenPos(internal_cursor);
    cursor.x += 28.0f;

    ImGui::PopStyleVar();
  }

  // Draw component name
  draw_list.AddText(EditorStyles::GetFonts().h4_bold, EditorSizes::h4_font_size,
                    cursor, EditorColor::text, identifier.c_str());

  // Draw remove button
  if (removed_ptr) {
    float button_size = 20.0f;
    cursor.x = p1.x - button_size - title_padding.x;

    if (IMComponents::IconButton(ICON_FA_XMARK, draw_list, cursor,
                                 ImVec2(-1.0f, 2.0f), IM_COL32(0, 0, 0, 0),
                                 IM_COL32(255, 65, 65, 90))) {
      *removed_ptr = true;
    }
  }

  // Advance cursor and draw content
  bool currently_opened = always_opened ? true : *opened_ptr;

  if (currently_opened) {
    ImGui::Dummy(ImVec2(size.x, size.y + y_margin + 12.0f));
  } else {
    ImGui::Dummy(ImVec2(size.x, size.y + y_margin));
  }

  return currently_opened;
}

void _EndComponent() {
  ImGui::Dummy(ImVec2(0.0f, 20.0f));
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
