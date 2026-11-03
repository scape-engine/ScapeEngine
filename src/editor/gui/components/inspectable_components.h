#ifndef INSPECTABLE_COMPONENTS_H
#define INSPECTABLE_COMPONENTS_H

#include "editor/registry/component_registry.h"
#include "engine/ecs/ecs_collection.h"

namespace InspectableComponents {

// Inspectable ECS components
void DrawTransform(Entity entity, TransformComponent& transform);
void DrawMeshRenderer(Entity entity, MeshRendererComponent& renderer);
void DrawCamera(Entity entity, CameraComponent& camera);
void DrawDirectionalLightComponent(
    Entity entity, DirectionalLightComponent& directional_light);
void DrawPointLightComponent(Entity entity, PointLightComponent& point_light);
void DrawSpotlightComponent(Entity entity, SpotlightComponent& spotlight);
void DrawSkylightComponent(Entity entity, SkyLightComponent& skylight);
void DrawVolumeComponent(Entity entity, VolumeComponent& volume);
// TODO: velocity component, box collider, sphere collider, rigidbody, audio
// listener, audio source

// TODO: Inspectable post-processing components

}  // namespace InspectableComponents

#endif  // INSPECTABLE_COMPONENTS_H