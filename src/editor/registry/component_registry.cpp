#include "component_registry.h"

#include <cctype>
#include <glm/gtc/type_ptr.hpp>
#include <string>

#include "editor/gizmos/component_gizmos.h"
#include "editor/gui/components/inspectable_components.h"

#include "engine/renderer/icons/icon_loader.h"
#include "engine/transform/transform.h"

namespace ComponentRegistry {

// Global registry for ecs components
std::unordered_map<std::string, ComponentInfo> g_component_registry;
std::vector<std::string> g_keys_ordered;

// Helper to convert "Mesh Renderer" -> "mesh_renderer"
// TODO: move to engine/resources/format.h
std::string ToSnakeCase(const std::string& name) {
  std::string result;
  for (char c : name) {
    if (c == ' ') {
      result += '_';
    } else {
      result += static_cast<char>(std::tolower(c));
    }
  }
  return result;
}

// Register a component type
template <typename T>
void RegisterComponent(
    const std::string& name, std::function<void(Entity, T&)> draw_inspectable,
    std::optional<std::function<void(IMGizmo&, TransformComponent&, T&)>>
        draw_gizmo,
    bool has_scene_icon) {

  std::string icon = ToSnakeCase(name);
  g_component_registry[name] = {
      // Name
      name,

      // Icon
      icon,

      // ? Description (empty for now)
      "",

      // Has
      [](Entity entity) { return ECS::Main().Has<T>(entity); },

      // Add
      [](Entity entity) { ECS::Main().Add<T>(entity); },

      // Try draw inspectable
      [draw_inspectable](Entity entity) {
        if (!ECS::Main().Has<T>(entity))
          return;
        draw_inspectable(entity, ECS::Main().Get<T>(entity));
      },

      // Try draw gizmo
      [draw_gizmo](Entity entity, IMGizmo& gizmo) {
        if (!draw_gizmo.has_value())
          return;
        if (!ECS::Main().Has<T>(entity))
          return;
        draw_gizmo.value()(gizmo, ECS::Main().Get<TransformComponent>(entity),
                           ECS::Main().Get<T>(entity));
      },

      // Try draw scene icons
      [has_scene_icon, icon](IMGizmo& gizmos,
                             TransformComponent& camera_transform) {
        if (!has_scene_icon)
          return;

        // Looked up each frame, scene icons load asynchronously
        uint32_t scene_icon = IconLoader::GetIconHandle(icon + "_gizmo");

        for (auto [entity, transform, component] :
             ECS::Main().View<TransformComponent, T>().each()) {
          gizmos.Icon3D(scene_icon,
                        Transform::GetPosition(transform, Space::WORLD),
                        camera_transform);
        }
      }};

  g_keys_ordered.push_back(name);
}

void Create() {
  g_component_registry.clear();
  g_keys_ordered.clear();

  RegisterComponent<TransformComponent>(
      "Transform", InspectableComponents::DrawTransform, std::nullopt, false);

  RegisterComponent<MeshRendererComponent>(
      "Mesh Renderer", InspectableComponents::DrawMeshRenderer, std::nullopt,
      false);

  RegisterComponent<CameraComponent>(
      "Camera", InspectableComponents::DrawCamera,
      std::nullopt,  // TODO: ComponentGizmos::DrawCamera
      true);

  RegisterComponent<PointLightComponent>(
      "Point Light", InspectableComponents::DrawPointLightComponent,
      std::nullopt,  // TODO: ComponentGizmos::DrawPointLight
      true);

  RegisterComponent<SpotlightComponent>(
      "Spotlight", InspectableComponents::DrawSpotlightComponent,
      std::nullopt,  // TODO: ComponentGizmos::DrawSpotlight
      true);

  RegisterComponent<DirectionalLightComponent>(
      "Directional Light", InspectableComponents::DrawDirectionalLightComponent,
      std::nullopt,  // TODO: ComponentGizmos::DrawDirectionalLight
      true);

  RegisterComponent<SkyLightComponent>(
      "Skylight", InspectableComponents::DrawSkylightComponent, std::nullopt,
      true);

  RegisterComponent<VolumeComponent>(
      "Volume", InspectableComponents::DrawVolumeComponent, std::nullopt, true);

  // TODO: velocity blur component, box collider, sphere collider, rigidbody,
  // audio listener, audio source
}

const std::unordered_map<std::string, ComponentInfo>& Get() {
  return g_component_registry;
}

const std::vector<std::string>& OrderedKeys() {
  return g_keys_ordered;
}

}  // namespace ComponentRegistry