#include "transformation.h"

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
namespace Transformation {

/* glm::vec3 Swap(const glm::vec3& position) {
  return position;
}

glm::quat Swap(const glm::quat& rotation) {
  return rotation;
}*/

glm::mat4 Model(const glm::vec3& position, const glm::quat& rotation,
                const glm::vec3& scale) {
  glm::mat4 model(1.0f);
  model = glm::translate(model, position);
  model *= glm::mat4_cast(glm::normalize(rotation));
  model = glm::scale(model, scale);
  return model;
}

// Left-handed view matrix (Y-up, Z-forward, X-right)
// TODO: render camera-relative (subtract the camera position in double before
// the float matrices); world-space floats snap by ~0.5 m at Earth radius
glm::mat4 View(const glm::vec3& camera_position,
               const glm::quat& camera_rotation) {
  glm::quat rotation = glm::normalize(camera_rotation);

  // Extract basis vectors
  glm::vec3 right = rotation * glm::vec3(1.0f, 0.0f, 0.0f);    // +X
  glm::vec3 up = rotation * glm::vec3(0.0f, 1.0f, 0.0f);       // +Y
  glm::vec3 forward = rotation * glm::vec3(0.0f, 0.0f, 1.0f);  // +Z

  // Left-handed view matrix
  glm::mat4 view(1.0f);

  // Row 0: right
  view[0][0] = right.x;
  view[1][0] = right.y;
  view[2][0] = right.z;
  view[3][0] = -glm::dot(right, camera_position);

  // Row 1: up
  view[0][1] = up.x;
  view[1][1] = up.y;
  view[2][1] = up.z;
  view[3][1] = -glm::dot(up, camera_position);

  // Row 2: forward
  view[0][2] = forward.x;
  view[1][2] = forward.y;
  view[2][2] = forward.z;
  view[3][2] = -glm::dot(forward, camera_position);

  return view;
}

glm::mat4 Projection(float fov, float aspect, float near_plane,
                     float far_plane) {
  float tanHalfFov = glm::tan(glm::radians(fov) * 0.5f);

  // originBottomLeft = true  (OpenGL): no flip needed
  // originBottomLeft = false (Metal/Vulkan/D3D): flip Y
  float yDir = bgfx::getCaps()->originBottomLeft ? 1.0f : -1.0f;

  glm::mat4 result(0.0f);
  result[0][0] = 1.0f / (aspect * tanHalfFov);
  result[1][1] = yDir / tanHalfFov;  // Flip based on backend
  // Reversed depth, near at 1 and far at 0: float depth's precision near 0
  // offsets the projection's loss far away, so coasts a metre under the sea
  // stay apart at km distances. Depth tests are GREATER, clears are 0
  result[2][2] = near_plane / (near_plane - far_plane);
  result[2][3] = 1.0f;
  result[3][2] = (near_plane * far_plane) / (far_plane - near_plane);

  return result;
}

glm::mat4 Normal(const glm::mat4& model) {
  return glm::transpose(glm::inverse(glm::mat3(model)));
}

}  // namespace Transformation
