#include "transform.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "engine/core/logger.h"
#include "engine/ecs/world.h"
#include "engine/math/transformation.h"

namespace Transform {
bool IsRoot(TransformComponent& transform) {
  return transform.depth_ == 0;
}

bool HasParent(TransformComponent& transform) {
  return transform.parent_ != entt::null &&
         ECS::Main().Has<TransformComponent>(transform.parent_);
}

TransformComponent& GetParent(TransformComponent& transform) {
  return ECS::Main().Get<TransformComponent>(transform.parent_);
}

void Evaluate(TransformComponent& transform) {
  transform.model_ = Transformation::Model(
      transform.position_, transform.rotation_, transform.scale_);
  transform.normal_ = Transformation::Normal(transform.model_);

  // DEBUG: Log model matrix Z-axis (forward direction in backend space)
  /* Logger::getInstance().Log(LogLevel::Debug,
                            "Model Z-axis: (" +
                                std::to_string(transform.model_[2][0]) + ", " +
                                std::to_string(transform.model_[2][1]) + ", " +
                                std::to_string(transform.model_[2][2]) + ")");
   */
}

void Evaluate(TransformComponent& transform, TransformComponent& parent) {
  transform.model_ = parent.model_ * Transformation::Model(transform.position_,
                                                           transform.rotation_,
                                                           transform.scale_);
  transform.normal_ = Transformation::Normal(transform.model_);

  // DEBUG: Log model matrix Z-axis (forward direction in backend space)
  /* Logger::getInstance().Log(LogLevel::Debug,
                            "Model Z-axis (child): (" +
                                std::to_string(transform.model_[2][0]) + ", " +
                                std::to_string(transform.model_[2][1]) + ", " +
                                std::to_string(transform.model_[2][2]) + ")");*/
}

void UpdateMVP(TransformComponent& transform, const glm::mat4& viewProjection) {
  transform.mvp_ = viewProjection * transform.model_;
}

// Helper for updating parent chain (TODO: optimize with caching?)
void _TmpUpdateModel(TransformComponent& transform) {
  if (HasParent(transform)) {
    TransformComponent& parent = GetParent(transform);
    _TmpUpdateModel(parent);
    Evaluate(transform, parent);
  } else {
    Evaluate(transform);
  }
}

void SetPosition(TransformComponent& transform, const glm::vec3& position,
                 Space space) {

  // Local space
  if (space == Space::LOCAL || !HasParent(transform)) {
    transform.position_ = position;
  } else {

    // World space with parent
    TransformComponent& parent = GetParent(transform);
    _TmpUpdateModel(parent);
    glm::vec4 localBackendPos = glm::vec4(position, 1.0f);
    glm::vec3 worldBackendPos =
        glm::vec3(glm::inverse(parent.model_) * localBackendPos);
    transform.position_ = worldBackendPos;
  }
  transform.modified_ = true;
}

void SetRotation(TransformComponent& transform, const glm::quat& rotation,
                 Space space) {

  // Local space
  if (space == Space::LOCAL || !HasParent(transform)) {
    transform.rotation_ = rotation;
    transform.euler_angles_ = ToEuler(rotation);
  } else {

    // World space with parent
    TransformComponent& parent = GetParent(transform);
    _TmpUpdateModel(parent);
    transform.rotation_ = glm::inverse(parent.rotation_) * rotation;
    transform.euler_angles_ = ToEuler(transform.rotation_);
  }
  transform.modified_ = true;
}

void SetEulerAngles(TransformComponent& transform, const glm::vec3& eulerAngles,
                    Space space) {
  glm::quat rotation = glm::quat(glm::radians(eulerAngles));

  // Local space
  if (space == Space::LOCAL || !HasParent(transform)) {
    transform.rotation_ = rotation;
    transform.euler_angles_ = eulerAngles;
  } else {

    // World space with parent
    TransformComponent& parent = GetParent(transform);
    _TmpUpdateModel(parent);
    transform.rotation_ = glm::inverse(parent.rotation_) * rotation;
    transform.euler_angles_ = ToEuler(transform.rotation_);
  }
  transform.modified_ = true;
}

void SetScale(TransformComponent& transform, const glm::vec3& scale,
              Space space) {

  // Local space
  if (space == Space::LOCAL || !HasParent(transform)) {
    transform.scale_ = scale;
  } else {

    // World space with parent
    TransformComponent& parent = GetParent(transform);
    _TmpUpdateModel(parent);
    glm::mat4 parentModel = parent.model_;
    glm::vec3 parentWorldScale(glm::length(glm::vec3(parentModel[0])),
                               glm::length(glm::vec3(parentModel[1])),
                               glm::length(glm::vec3(parentModel[2])));
    transform.scale_ = scale / parentWorldScale;
  }
  transform.modified_ = true;
}

glm::vec3 GetPosition(TransformComponent& transform, Space space) {

  // Local space
  if (space == Space::LOCAL || !HasParent(transform)) {
    return transform.position_;
  } else {

    // World space with parent
    return glm::vec3(transform.model_[3]);
  }
}

glm::quat GetRotation(TransformComponent& transform, Space space) {

  // Local space
  if (space == Space::LOCAL || !HasParent(transform)) {
    return transform.rotation_;
  } else {

    // World space with parent
    glm::mat3 rotationMatrix(transform.model_);
    glm::vec3 col0 = glm::normalize(glm::vec3(rotationMatrix[0]));
    glm::vec3 col1 = glm::normalize(glm::vec3(rotationMatrix[1]));
    glm::vec3 col2 = glm::normalize(glm::vec3(rotationMatrix[2]));
    rotationMatrix[0] = col0;
    rotationMatrix[1] = col1;
    rotationMatrix[2] = col2;
    return glm::quat_cast(rotationMatrix);
  }
}

glm::vec3 GetEulerAngles(TransformComponent& transform, Space space) {

  // Local space
  if (space == Space::LOCAL || !HasParent(transform)) {
    return transform.euler_angles_;
  } else {

    // Extract world-space euler angles from world rotation
    glm::quat worldRot = GetRotation(transform, Space::WORLD);
    return ToEuler(worldRot);
  }
}

glm::vec3 GetScale(TransformComponent& transform, Space space) {

  // Local space
  if (space == Space::LOCAL || !HasParent(transform)) {
    return transform.scale_;
  } else {

    // World space with parent
    return glm::vec3(glm::length(glm::vec3(transform.model_[0])),
                     glm::length(glm::vec3(transform.model_[1])),
                     glm::length(glm::vec3(transform.model_[2])));
  }
}

void Translate(TransformComponent& transform, const glm::vec3& position,
               Space space) {

  // Local space
  if (space == Space::LOCAL || !HasParent(transform)) {
    transform.position_ += position;
  } else {

    // World space with parent
  }
  transform.modified_ = true;
}

void Rotate(TransformComponent& transform, const glm::quat& rotation,
            Space space) {

  // Local space
  if (space == Space::LOCAL || !HasParent(transform)) {
    transform.rotation_ = rotation * transform.rotation_;
  } else {

    // World space with parent
  }
  transform.modified_ = true;
}

void Scale(TransformComponent& transform, const glm::vec3& scale, Space space) {

  // Local space
  if (space == Space::LOCAL || !HasParent(transform)) {
    transform.scale_ *= scale;
  } else {

    // World space with parent
  }
  transform.modified_ = true;
}

// Direction helpers
glm::vec3 _Direction(const glm::vec3& base, TransformComponent& transform,
                     Space space) {
  if (space == Space::LOCAL) {
    return glm::rotate(transform.rotation_, base);
  } else {
    return glm::rotate(GetRotation(transform, Space::WORLD), base);
  }
}

// Describe convention: Y-up, Z-forward, X-right (left-handed)
glm::vec3 Forward(TransformComponent& transform, Space space) {
  return _Direction(glm::vec3(0.0f, 0.0f, 1.0f), transform, space);  // +Z
}
glm::vec3 Backward(TransformComponent& transform, Space space) {
  return _Direction(glm::vec3(0.0f, 0.0f, -1.0f), transform, space);  // -Z
}
glm::vec3 Right(TransformComponent& transform, Space space) {
  return _Direction(glm::vec3(1.0f, 0.0f, 0.0f), transform, space);  // +X
}
glm::vec3 Left(TransformComponent& transform, Space space) {
  return _Direction(glm::vec3(-1.0f, 0.0f, 0.0f), transform, space);  // -X
}
glm::vec3 Up(TransformComponent& transform, Space space) {
  return _Direction(glm::vec3(0.0f, 1.0f, 0.0f), transform, space);  // +Y
}
glm::vec3 Down(TransformComponent& transform, Space space) {
  return _Direction(glm::vec3(0.0f, -1.0f, 0.0f), transform, space);  // -Y
}

glm::quat LookAt(const glm::vec3& position, const glm::vec3& target) {
  glm::vec3 direction = glm::normalize(target - position);
  return glm::rotation(glm::vec3(0, 0, 1), direction);  // +Z is forward
}

glm::quat ToQuat(const glm::vec3& eulerAngles) {
  return glm::quat(glm::radians(eulerAngles));
}

glm::vec3 ToEuler(const glm::quat& quaternion) {
  return glm::degrees(glm::eulerAngles(quaternion));
}
}  // namespace Transform