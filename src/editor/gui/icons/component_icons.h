#ifndef COMPONENT_ICONS_H
#define COMPONENT_ICONS_H

#include <type_traits>

#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"
#include "engine/ecs/components/camera_component.h"
#include "engine/ecs/components/light_component.h"
#include "engine/ecs/components/mesh_renderer_component.h"
#include "engine/ecs/components/volume_component.h"
#include "engine/ecs/world.h"

namespace EditorIcons {

// Icon of a component type, for hierarchy rows and inspector headers
template <typename T>
const char* GetComponentIcon() {
  if constexpr (std::is_same_v<T, CameraComponent>)
    return ICON_FA_VIDEO;
  else if constexpr (std::is_same_v<T, PointLightComponent> ||
                     std::is_same_v<T, DirectionalLightComponent> ||
                     std::is_same_v<T, SkyLightComponent>)
    return ICON_FA_LIGHTBULB;
  else if constexpr (std::is_same_v<T, MeshRendererComponent>)
    return ICON_FA_CUBE;
  else if constexpr (std::is_same_v<T, VolumeComponent>)
    return ICON_FA_DIAGRAM_PROJECT;
  else
    return ICON_FA_CIRCLE_NODES;  // empty / transform-only entity
}

// Icon of the first of Ts the entity has; the fallback icon otherwise
template <typename... Ts>
const char* GetFirstComponentIcon(Entity entity) {
  const char* icon = nullptr;
  ((icon == nullptr && ECS::Main().Has<Ts>(entity)
        ? (icon = GetComponentIcon<Ts>(), void())
        : void()),
   ...);
  return icon ? icon : GetComponentIcon<void>();
}

// Icon of an entity: its most significant component, in priority order
inline const char* GetEntityIcon(Entity entity) {
  return GetFirstComponentIcon<CameraComponent, PointLightComponent,
                               DirectionalLightComponent, SkyLightComponent,
                               MeshRendererComponent, VolumeComponent>(entity);
}

}  // namespace EditorIcons

#endif  // COMPONENT_ICONS_H