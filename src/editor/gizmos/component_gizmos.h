#ifndef COMPONENT_GIZMOS_H
#define COMPONENT_GIZMOS_H

#include <imgui.h>

#include "engine/ecs/world.h"
#include "engine/renderer/gizmos/imgizmo.h"

namespace ComponentGizmos {
// Draws debug shapes for components (wireframes, icons, etc.) with IMGizmo

void DrawSceneViewIcons(IMGizmo& gizmos, TransformComponent& camera_transform);

// Shapes of the entity's components: light reach and direction, camera
// frustum. Called for the selected entity only
void DrawEntityGizmos(IMGizmo& gizmos, Entity entity, bool draw_cameras,
                      bool draw_lights, float camera_aspect);

// COMPONENTS:
void DrawCamera(IMGizmo& gizmos, TransformComponent& transform,
                CameraComponent& camera, float aspect);
void DrawDirectionalLight(IMGizmo& gizmos, TransformComponent& transform,
                          DirectionalLightComponent& light);
void DrawPointLight(IMGizmo& gizmos, TransformComponent& transform,
                    PointLightComponent& light);
void DrawSpotlight(IMGizmo& gizmos, TransformComponent& transform,
                   SpotlightComponent& light);
// TODO: void DrawBoxCollider(IMGizmo& gizmos, TransformComponent& transform,
// BoxColliderComponent& collider); void DrawSphereCollider(IMGizmo& gizmos,
// TransformComponent& transform, SphereColliderComponent& sphereCollider);
// void DrawAudioSource(IMGizmo& gizmos, TransformComponent& transform,
// AudioSourceComponent& audioSource);
}  // namespace ComponentGizmos

#endif  // COMPONENT_GIZMOS_H
