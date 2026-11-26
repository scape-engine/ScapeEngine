#include "inspectable_components.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "editor/editor_ui.h"
#include "editor/events.h"
#include "editor/runtime/runtime.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"

#include "generator_properties.h"
#include "im_components.h"

#include "engine/animation/animation.h"
#include "engine/ecs/components/instance_source_component.h"
#include "engine/ecs/components/instanced_mesh_component.h"
#include "engine/context/engine_context.h"  // ! should be removed?
#include "engine/pcg/generator_resource.h"  // ! should be removed?
#include "engine/pcg/stage.h"
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
// Components open at once: a section inside one is a foldout, not a box
int g_component_depth = 0;
// Whether each open section is a foldout, innermost last
std::vector<bool> g_section_foldouts;

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
// Whether the entity drawn may lose components (AllowRemoving)
static bool g_allow_removing = true;

void AllowRemoving(bool allow) { g_allow_removing = allow; }

bool _BeginComponent(const std::string& identifier,
                     uint32_t icon,  // TODO: Use IconPool texture ID
                     bool* enabled_ptr = nullptr, bool* removed_ptr = nullptr,
                     bool always_opened = false) {
  if (!g_allow_removing)
    removed_ptr = nullptr;
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
      p0, p1, EditorColor::surface_3, radius,
      opened ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll);
  if (!opened)
    draw_list.AddRect(p0, p1, EditorColor::line_strong, radius, 0, 1.0f);

  // CHEVRON
  float x = p0.x + pad_x;
  if (!always_opened) {
    IMComponents::Glyph(draw_list, ImVec2(x, p0.y),
                        ImVec2(12.0f, header_height),
                        opened ? ICON_FA_CHEVRON_DOWN : ICON_FA_CHEVRON_RIGHT,
                        EditorColor::chevron, small);
    x += 12.0f + 8.0f;
  }

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
  ++g_component_depth;
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
  --g_component_depth;

  // Drawn on the parent list, which renders before the child: ends up behind
  ImDrawList& draw_list = *ImGui::GetWindowDrawList();
  const float radius = 6.0f;
  const ImVec2 body_min = ImGui::GetItemRectMin();
  const ImVec2 body_max = ImGui::GetItemRectMax();

  draw_list.AddRectFilled(body_min, body_max, EditorColor::surface_2, radius,
                          ImDrawFlags_RoundCornersBottom);

  draw_list.AddRect(ImVec2(body_min.x, g_component_top.y), body_max,
                    EditorColor::line_strong, radius, 0, 1.0f);

  ImGui::Dummy(ImVec2(0.0f, 4.0f));  // gap to next component
}

//=============================================================================
// HELPER WIDGETS
//=============================================================================
void _Headline(const std::string& label) {
  IMComponents::Label(label, EditorStyles::GetFonts().h4_bold);
  ImGui::Dummy(ImVec2(0.0f, 1.0f));
}

void Headline(const std::string& label) {
  _Headline(label);
}

void _SpacingS() {
  ImGui::Dummy(ImVec2(0.0f, 3.0f));
}

void _SpacingM() {
  ImGui::Dummy(ImVec2(0.0f, 10.0f));
}

bool BeginSection(const std::string& title, bool always_opened) {
  const bool foldout = g_component_depth > 0;
  const bool opened =
      foldout ? always_opened || IMComponents::Foldout(title.c_str())
              : _BeginComponent(title, 0, nullptr, nullptr, always_opened);
  if (opened)
    g_section_foldouts.push_back(foldout);
  return opened;
}

void EndSection() {
  const bool foldout = g_section_foldouts.back();
  g_section_foldouts.pop_back();
  if (foldout)
    _SpacingS();
  else
    _EndComponent();
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

void DrawSkylightComponent(Entity entity, SkyLightComponent& skylight) {
  bool removed = false;

  if (_BeginComponent("Skylight", 0, &skylight.enabled_, &removed)) {
    _Headline("Properties");

    IMComponents::Input("Intensity", skylight.intensity_);
    IMComponents::ColorPicker("Color", skylight.color_);

    _SpacingS();
    _Headline("Source");

    // Every edit below needs a new capture
    bool source_changed = false;

    int source = static_cast<int>(skylight.source_type_);
    const char* sources[] = {"Captured Scene", "Specified Cubemap"};
    if (ImGui::Combo("Source Type", &source, sources, IM_ARRAYSIZE(sources))) {
      skylight.source_type_ = static_cast<SkyLightSource>(source);
      source_changed = true;
    }

    if (skylight.source_type_ == SkyLightSource::SourceCubemap) {
      // Equirect image dropped from the asset browser
      const std::string file =
          skylight.cubemap_path_.empty()
              ? "None (drop a texture)"
              : std::filesystem::path(skylight.cubemap_path_)
                    .filename()
                    .string();
      IMComponents::Label("Cubemap: " + file);

      if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("TEXTURE_FILE")) {
          std::string relative(static_cast<const char*>(payload->Data));
          skylight.cubemap_path_ =
              Runtime::Project().AbsolutePath(relative).string();
          source_changed = true;
        }
        ImGui::EndDragDropTarget();
      }

      if (!skylight.cubemap_path_.empty() &&
          ImGui::Button(ICON_FA_XMARK " Clear")) {
        skylight.cubemap_path_.clear();
        source_changed = true;
      }

      if (ImGui::SliderFloat("Source Cubemap Angle",
                             &skylight.source_cubemap_angle_, 0.0f, 360.0f,
                             "%.0f deg")) {
        source_changed = true;
      }
    } else {
      IMComponents::Input("Real-time Capture", skylight.real_time_capture_);
      if (!skylight.real_time_capture_ &&
          IMComponents::ButtonBig("Recapture", "Capture the sky again")) {
        skylight.recapture_ = true;
      }
    }

    _SpacingS();
    _Headline("Lower Hemisphere");

    // Applied when captured, like the source
    bool solid = skylight.lower_hemisphere_is_solid_color_;
    glm::vec3 color = skylight.lower_hemisphere_color_;
    IMComponents::Input("Solid Color", skylight.lower_hemisphere_is_solid_color_);
    IMComponents::ColorPicker("Color", skylight.lower_hemisphere_color_);
    source_changed |= solid != skylight.lower_hemisphere_is_solid_color_ ||
                      color != skylight.lower_hemisphere_color_;

    if (source_changed) {
      skylight.recapture_ = true;
    }

    _EndComponent();
  }

  if (removed)
    _EcsRemove<SkyLightComponent>(entity);
}

void DrawAnimatorSettings(AnimatorSettings& settings) {
  IMComponents::Input("Plant Feet", settings.plant_feet);
  ImGui::DragFloat("Plant Distance", &settings.plant_distance, 1.0f, 0.0f,
                   1.0e4f, "%.0f m");
  ImGui::SliderFloat("Plant Reach", &settings.plant_reach, 0.0f, 1.0f);
  ImGui::TextDisabled("A foot moves at most this share of its leg");
  ImGui::DragFloat("Full Rate Distance", &settings.full_rate_distance, 1.0f,
                   1.0f, 1.0e4f, "%.0f m");
  ImGui::SliderInt("Max Step", &settings.max_step, 1, 32);
  ImGui::TextDisabled("Farther ones sample every few frames, up to this");
  ImGui::DragFloat("Sample Distance", &settings.sample_distance, 1.0f, 0.0f,
                   1.0e5f, "%.0f m");
  ImGui::TextDisabled("Farther ones don't sample; 0: no limit");
}

void DrawAnimatorComponent(Entity entity, AnimatorComponent& animator) {
  bool removed = false;

  if (_BeginComponent("Animator", 0, &animator.playing_, &removed)) {
    _Headline("Clip");

    if (animator.data_ && !animator.data_->clips.empty()) {
      const auto& clips = animator.data_->clips;
      const bool valid =
          animator.clip_ >= 0 && animator.clip_ < static_cast<int>(clips.size());
      if (ImGui::BeginCombo("Clip",
                            valid ? clips[animator.clip_].name.c_str()
                                  : "Bind pose")) {
        for (int i = 0; i < static_cast<int>(clips.size()); ++i)
          if (ImGui::Selectable(clips[i].name.c_str(), i == animator.clip_))
            Animation::Play(animator, i, animator.fade_length_);
        ImGui::EndCombo();
      }
      if (valid)
        ImGui::SliderFloat("Time", &animator.time_, 0.0f,
                           clips[animator.clip_].duration, "%.2f s");
    } else {
      ImGui::TextDisabled("The model has no clips");
    }
    IMComponents::Input("Speed", animator.speed_, 0.01f);
    IMComponents::Input("Loop", animator.loop_);

    // The seeded pose layers: where each sits in its clip, and how strongly
    if (!animator.poses_.empty()) {
      _SpacingS();
      _Headline("Poses");
      bool changed = false;
      for (size_t i = 0; i < animator.poses_.size(); ++i) {
        PoseSampler::Layer& layer = animator.poses_[i];
        ImGui::PushID(static_cast<int>(i));
        const std::string label = "Pose " + std::to_string(i + 1);
        changed |= ImGui::SliderFloat(label.c_str(), &layer.ratio, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Weight", &layer.weight, 0.0f, 2.0f);
        ImGui::PopID();
      }
      if (changed)
        Animation::Resample(animator);
    }

    // The model's gaits, shared by all its instances: the clip each plays,
    // the ground it covers a second at scale 1, and its rates
    if (animator.gaits_ && animator.data_) {
      _SpacingS();
      _Headline("Gaits");
      GaitSet& set = *animator.gaits_;
      const auto& clips = animator.data_->clips;
      ImGui::DragFloat("Speed Scale", &set.speed_scale, 0.01f, 0.0f, 100.0f,
                       "%.2fx");
      ImGui::TextDisabled("Shared by every instance of the model");
      for (size_t i = 0; i < set.gaits.size(); ++i) {
        GaitSet::Gait& gait = set.gaits[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::SeparatorText(gait.name.empty() ? "Gait" : gait.name.c_str());
        const bool valid = gait.clip_index >= 0 &&
                           gait.clip_index < static_cast<int>(clips.size());
        if (ImGui::BeginCombo("Clip", valid ? clips[gait.clip_index].name.c_str()
                                            : "None")) {
          for (int c = 0; c < static_cast<int>(clips.size()); ++c)
            if (ImGui::Selectable(clips[c].name.c_str(), c == gait.clip_index)) {
              gait.clip_index = c;
              gait.clip = clips[c].name;
            }
          ImGui::EndCombo();
        }
        ImGui::DragFloat("Pace", &gait.pace, 0.01f, 0.0f, 100.0f, "%.2f m/s");
        ImGui::DragFloatRange2("Rates", &gait.min_rate, &gait.max_rate, 0.01f,
                               0.0f, 10.0f, "%.2fx");
        ImGui::PopID();
      }
    }

    _SpacingS();
    _Headline("Sampling");
    DrawAnimatorSettings(animator.settings_);

    _EndComponent();
  }

  if (removed)
    _EcsRemove<AnimatorComponent>(entity);
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

// The object an instance is part of: what made it; null: unrecorded
static const InstanceSourceComponent::Source* _SourceOf(Entity entity,
                                                       int instance) {
  auto& ecs = ECS::Main();
  const auto* sources = ecs.Reg().try_get<InstanceSourceComponent>(entity);
  const auto* set = ecs.Reg().try_get<InstancedMeshComponent>(entity);
  if (!sources)
    return nullptr;
  // An object made as an entity of its own
  if (!set)
    return sources->sources_.size() == 1 ? &sources->sources_.front()
                                         : nullptr;
  if (set->anchors_.size() != set->locals_.size())
    return nullptr;
  return sources->Find(set->anchors_[instance]);
}

std::string InstanceName(Entity entity, int instance) {
  const auto* source = _SourceOf(entity, instance);
  if (!source)
    return "Instance " + std::to_string(instance);
  // "procmodel:models/rock.json": rock
  const size_t colon = source->id.find(':');
  return std::filesystem::path(colon == std::string::npos
                                   ? source->id
                                   : source->id.substr(colon + 1))
      .stem()
      .string();
}

void DrawInstance(Entity entity, int instance,
                  const std::function<void()>& fields) {
  if (!_BeginComponent("Instance", 0))
    return;
  const auto text = [](const std::string& value) {
    return [value] {
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(value.c_str());
      return false;
    };
  };
  const auto* source = _SourceOf(entity, instance);
  if (source)
    IMComponents::Row("Source", text(source->id),
                      "What it was made from, relative to the project");
  // Fields given in place of its details
  if (fields) {
    fields();
    _EndComponent();
    return;
  }
  if (source)
    IMComponents::Row("Seed", text(std::to_string(source->seed)));
  // Its object's origin in the world
  const auto* set = ECS::Main().Reg().try_get<InstancedMeshComponent>(entity);
  const glm::vec3 local = !set ? glm::vec3(0.0f)
                          : set->anchors_.size() == set->locals_.size()
                              ? set->anchors_[instance]
                              : glm::vec3(set->locals_[instance][3]);
  const glm::vec3 at = ECS::Main().Get<TransformComponent>(entity).model_ *
                       glm::vec4(local, 1.0f);
  char position[96];
  std::snprintf(position, sizeof(position), "%.2f, %.2f, %.2f", at.x, at.y,
                at.z);
  IMComponents::Row("Position", text(position));
  _EndComponent();
}

GeneratedOrigin FindOrigin(Entity entity) {
  // Up its sources to the volume, the first stage named on the way
  auto& ecs = ECS::Main();
  GeneratedOrigin origin;
  for (const auto* instance =
           ecs.Reg().try_get<ProceduralInstanceComponent>(entity);
       instance;) {
    if (origin.stage.empty())
      origin.stage = instance->stage_;
    const Entity source = instance->source_;
    if (!ecs.Reg().valid(source))
      break;
    if (ecs.Has<VolumeComponent>(source)) {
      origin.volume = source;
      break;
    }
    instance = ecs.Reg().try_get<ProceduralInstanceComponent>(source);
  }
  return origin;
}

PCG::Stage* FindStage(const GeneratedOrigin& origin) {
  if (origin.volume == entt::null || origin.stage.empty())
    return nullptr;
  const ResourceID id =
      ECS::Main().Get<VolumeComponent>(origin.volume).generator_id;
  auto resource =
      EngineContext::resourceManager().GetResourceAs<PCG::GeneratorResource>(
          id);
  return resource && resource->Get() ? resource->Get()->GetStage(origin.stage)
                                    : nullptr;
}

// The settings of the stage that made it, titled by its type; edits apply
// to its generator (the path under the entity's name leads there)
void DrawProceduralInstanceComponent(Entity entity,
                                     ProceduralInstanceComponent&) {
  const GeneratedOrigin origin = FindOrigin(entity);
  PCG::Stage* stage = FindStage(origin);
  if (!stage)
    return;
  if (_BeginComponent(PCG::StageTypeName(stage->type), 0, &stage->enabled)) {
    GeneratorProperties::DrawStage(
        ECS::Main().Get<VolumeComponent>(origin.volume).generator_id,
        origin.stage);
    _EndComponent();
  }
}

void DrawViewerComponent(Entity entity, ViewerComponent&) {
  bool removed = false;

  if (_BeginComponent("Viewer", 0, nullptr, &removed)) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled(
        "The world loads, picks its detail and simulates around this "
        "entity, as around the editor's camera");
    ImGui::PopTextWrapPos();
    _EndComponent();
  }

  if (removed)
    _EcsRemove<ViewerComponent>(entity);
}

void DrawFadeComponent(Entity entity, FadeComponent& fade) {
  // A generated entity's fades are its generator's paging and its stage's
  // on/off
  if (FindOrigin(entity).volume != entt::null)
    return;
  bool removed = false;

  if (_BeginComponent("Fade", 0, nullptr, &removed)) {
    IMComponents::Slider("Visibility", fade.visibility_, 0.0f, 1.0f, "%.2f",
                         "Children fade with the entity");
    IMComponents::Slider("Target", fade.target_, 0.0f, 1.0f);
    IMComponents::Drag("Duration", fade.duration_, 0.01f, 0.0f, 60.0f,
                       "%.2f s",
                       "Visibility moves to the target over this long");
    IMComponents::Input("Destroy When Hidden", fade.destroy_when_hidden_);
    _EndComponent();
  }

  if (removed)
    _EcsRemove<FadeComponent>(entity);
}

void DrawPCGWorldComponent(Entity entity, PCGWorldComponent&) {
  bool removed = false;

  if (_BeginComponent("PCG World", 0, nullptr, &removed)) {
    auto& ecs = ECS::Main();
    _Headline("Generators");
    bool any = false;
    for (const Entity child : ecs.Get<TransformComponent>(entity).children_) {
      if (!ecs.Has<VolumeComponent>(child))
        continue;
      any = true;
      auto generator =
          EngineContext::resourceManager()
              .GetResourceAs<PCG::GeneratorResource>(
                  ecs.Get<VolumeComponent>(child).generator_id);
      const std::string label =
          ecs.Get<TransformComponent>(child).name_ + "  (" +
          (generator ? PCG::GetGeneratorTypeName(generator->GetGeneratorType())
                     : "no generator") +
          ")##" + std::to_string(static_cast<uint32_t>(child));
      if (ImGui::Selectable(label.c_str()))
        Runtime::State().SelectEntity(child);
    }
    if (!any)
      ImGui::TextDisabled("No generators yet");

    _SpacingS();
    _Headline("Add Generator");
    for (const auto type :
         {PCG::GeneratorType::Planet, PCG::GeneratorType::Craft,
            PCG::GeneratorType::Region, PCG::GeneratorType::Graph}) {
      if (ImGui::Button(PCG::GetGeneratorTypeName(type))) {
        GeneratorProperties::AddGenerator(entity, type);
        Runtime::Project().MarkSceneDirty();
      }
      ImGui::SameLine();
    }
    ImGui::NewLine();
    _EndComponent();
  }

  if (removed)
    _EcsRemove<PCGWorldComponent>(entity);
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

    int32_t lod = static_cast<int32_t>(volume.lod_level);
    IMComponents::Input("LOD Level", lod, 0.05f);
    if (lod != static_cast<int32_t>(volume.lod_level)) {
      volume.lod_level = static_cast<uint8_t>(glm::clamp(lod, 0, 8));
      volume.dirty = true;
    }

    _SpacingS();
    _Headline("Generator");

    auto generator =
        EngineContext::resourceManager().GetResourceAs<PCG::GeneratorResource>(
            volume.generator_id);
    if (generator) {
      IMComponents::Label(
          std::string(PCG::GetGeneratorTypeName(generator->GetGeneratorType())) +
          " (ID " + std::to_string(volume.generator_id) + ")");

      if (generator->GetGeneratorType() == PCG::GeneratorType::Graph) {
        if (ImGui::Button("Open in PCG Editor"))
          EditorEvents::open_graph_editor(volume.generator_id);
      } else {
        GeneratorProperties::Draw(volume.generator_id);
      }
    } else {
      ImGui::TextDisabled("No generator selected");
      for (const auto type :
           {PCG::GeneratorType::Planet, PCG::GeneratorType::Craft,
            PCG::GeneratorType::Region, PCG::GeneratorType::Graph}) {
        if (ImGui::Button(PCG::GetGeneratorTypeName(type)))
          GeneratorProperties::Create(volume, type);
        ImGui::SameLine();
      }
      ImGui::NewLine();
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