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
void DrawPCGWorldComponent(Entity entity, PCGWorldComponent& world);
void DrawVolumeComponent(Entity entity, VolumeComponent& volume);
void DrawAnimatorComponent(Entity entity, AnimatorComponent& animator);
void DrawFadeComponent(Entity entity, FadeComponent& fade);

// The inspector's section look (a component's header bar and body), for
// asset inspectors too: draw the body and call EndSection when it returns true
bool BeginSection(const std::string& title, bool always_opened = false);
void EndSection();
// A bold label over a group of fields
void Headline(const std::string& label);

// Sampling and foot planting settings, also drawn for a planet's creatures
void DrawAnimatorSettings(AnimatorSettings& settings);
// TODO: velocity component, box collider, sphere collider, rigidbody, audio
// listener, audio source

// TODO: Inspectable post-processing components

}  // namespace InspectableComponents

#endif  // INSPECTABLE_COMPONENTS_H