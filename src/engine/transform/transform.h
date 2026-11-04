#ifndef TRANSFORM_H
#define TRANSFORM_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/ecs/ecs_collection.h"

enum class Space { LOCAL, WORLD };

namespace Transform {

// ──────────────── CORE FUNCTIONS ────────────────
bool IsRoot(TransformComponent& transform);
bool HasParent(TransformComponent& transform);

// Get parent; transform should have a parent before using this
TransformComponent& GetParent(TransformComponent& transform);

// Update model matrix (local or with parent)
void Evaluate(TransformComponent& transform);
void Evaluate(TransformComponent& transform, TransformComponent& parent);

// Update MVP matrix
void UpdateMVP(TransformComponent& transform, const glm::mat4& viewProjection);

// ──────────────── TRANSFORMATION ────────────────
void SetPosition(TransformComponent& transform, const glm::vec3& position,
                 Space space = Space::LOCAL);
void SetRotation(TransformComponent& transform, const glm::quat& rotation,
                 Space space = Space::LOCAL);
void SetEulerAngles(TransformComponent& transform, const glm::vec3& eulerAngles,
                    Space space = Space::LOCAL);
void SetScale(TransformComponent& transform, const glm::vec3& scale,
              Space space = Space::LOCAL);

glm::vec3 GetPosition(TransformComponent& transform,
                      Space space = Space::LOCAL);
glm::quat GetRotation(TransformComponent& transform,
                      Space space = Space::LOCAL);
glm::vec3 GetEulerAngles(TransformComponent& transform,
                         Space space = Space::LOCAL);
glm::vec3 GetScale(TransformComponent& transform, Space space = Space::LOCAL);

// Model matrix, evaluated from the local values (and the parent chain in world
// space), so it doesn't depend on the transform system having run
glm::mat4 GetModel(TransformComponent& transform, Space space = Space::LOCAL);
// Sets position, rotation and scale from a model matrix
void SetModel(TransformComponent& transform, const glm::mat4& model,
              Space space = Space::LOCAL);

void Translate(TransformComponent& transform, const glm::vec3& delta,
               Space space = Space::LOCAL);
void Rotate(TransformComponent& transform, const glm::quat& delta,
            Space space = Space::LOCAL);
void Scale(TransformComponent& transform, const glm::vec3& scale,
           Space space = Space::LOCAL);

// ──────────────── HELPERS ────────────────
glm::vec3 Forward(TransformComponent& transform, Space space = Space::LOCAL);
glm::vec3 Backward(TransformComponent& transform, Space space = Space::LOCAL);
glm::vec3 Right(TransformComponent& transform, Space space = Space::LOCAL);
glm::vec3 Left(TransformComponent& transform, Space space = Space::LOCAL);
glm::vec3 Up(TransformComponent& transform, Space space = Space::LOCAL);
glm::vec3 Down(TransformComponent& transform, Space space = Space::LOCAL);

// Quaternion for rotating from position to target
glm::quat LookAt(const glm::vec3& position, const glm::vec3& target);

// Euler <-> Quaternion conversions (degrees)
glm::quat ToQuat(const glm::vec3& eulerAngles);
glm::vec3 ToEuler(const glm::quat& quaternion);

}  // namespace Transform

#endif  // TRANSFORM_H
