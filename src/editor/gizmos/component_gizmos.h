#ifndef COMPONENT_GIZMOS_H
#define COMPONENT_GIZMOS_H

#include <imgui.h>

#include "engine/ecs/world.h"
#include "engine/renderer/gizmos/imgizmo.h"

namespace ComponentGizmos {
// TODO: Draw debug shapes for components (wireframes, icons, etc.)
// These will use a custom IMGizmo class for 3D debug rendering

void DrawSceneViewIcons(IMGizmo& gizmos, TransformComponent& camera_transform);
// void DrawEntityGizmos(IMGizmo& gizmos, EntityContainer& entity);

// COMPONENTS:
// void DrawCamera(IMGizmo& gizmos, TransformComponent& transform,
// CameraComponent& camera); void DrawBoxCollider(IMGizmo& gizmos,
// TransformComponent& transform, BoxColliderComponent& collider); void
// DrawSphereCollider(IMGizmo& gizmos, TransformComponent& transform,
// SphereColliderComponent& sphereCollider); void DrawPointLight(IMGizmo&
// gizmos, TransformComponent& transform, PointLightComponent& pointLight); void
// DrawSpotlight(IMGizmo& gizmos, TransformComponent& transform,
// SpotlightComponent& spotlight); void DrawAudioSource(IMGizmo& gizmos,
// TransformComponent& transform, AudioSourceComponent& audioSource);
}  // namespace ComponentGizmos

#endif  // COMPONENT_GIZMOS_H