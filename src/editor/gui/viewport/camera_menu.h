#ifndef CAMERA_MENU_H
#define CAMERA_MENU_H

#include "editor/camera/god_camera.h"
#include "engine/ecs/components/camera_component.h"

namespace Viewport {

// Contents of the Camera popup: the scene (editor) camera's settings
void CameraMenu(GodCameraState& state, TransformComponent& transform,
                CameraComponent& camera);

}  // namespace Viewport

#endif  // CAMERA_MENU_H