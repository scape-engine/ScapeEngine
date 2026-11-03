#include "component_gizmos.h"

#include <glm/gtc/type_ptr.hpp>

#include "editor/registry/component_registry.h"

namespace ComponentGizmos {
// TODO: Depends on IMGizmo debug drawing
// This will render 3D debug shapes (wireframes, icons) for various components

void DrawSceneViewIcons(IMGizmo& gizmos, TransformComponent& camera_transform) {
  // Try draw scene icon of every component instance
  for (const auto& [name, component] : ComponentRegistry::Get()) {
    component.try_draw_scene_icons(gizmos, camera_transform);
  }
}

/*  void DrawReferenceGrid(IMGizmo& gizmos, float gridSize, float gridSpacing) {
  // Set grid color
gizmos.color = glm::vec3(0.7f, 0.7f, 0.7f);
gizmos.opacity = 1.0f;


// Draw grid lines on XZ plane (Y=0)
for (float x = -gridSize; x <= gridSize; x += gridSpacing) {
  // Lines parallel to Z axis
  gizmos.Line(glm::vec3(x, 0.0f, -gridSize), glm::vec3(x, 0.0f, gridSize));
}

for (float z = -gridSize; z <= gridSize; z += gridSpacing) {
  // Lines parallel to X axis
  gizmos.Line(glm::vec3(-gridSize, 0.0f, z), glm::vec3(gridSize, 0.0f, z));
}

// Draw thicker center axes
gizmos.color = glm::vec3(0.4f, 0.4f, 0.4f);
gizmos.opacity = 0.6f;
gizmos.Line(glm::vec3(0.0f, 0.0f, -gridSize),
            glm::vec3(0.0f, 0.0f, gridSize));
gizmos.Line(glm::vec3(-gridSize, 0.0f, 0.0f),
            glm::vec3(gridSize, 0.0f, 0.0f));
gizmos.Plane(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1000.0f, 1.0f, 1000.0f));
}
*/

// stub:
/*
void DrawCamera(IMGizmo& gizmos, TransformComponent& transform, CameraComponent&
camera) { if (!camera.enabled_) return;

  gizmos.foreground = true;
  gizmos.color = EditorGizmoColor::CAMERA;
  gizmos.opacity = 0.8f;
  gizmos.boxWire(Transform::GetPosition(transform, Space::WORLD),
                 glm::vec3(2.0f),
                 Transform::GetRotation(transform, Space::WORLD));
}
*/
}  // namespace ComponentGizmos