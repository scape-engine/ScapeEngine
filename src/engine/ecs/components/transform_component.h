#ifndef TRANSFORM_COMPONENT_H
#define TRANSFORM_COMPONENT_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entt.hpp>

#include "engine/renderer/model/mesh.h"

using Entity = entt::entity;

/* ───────────────────────────────── TRANSFORM ───────────────────────────── */
struct TransformComponent
{
    // ───────── General ─────────
    uint32_t     id_        = 0;         // unique per-entity
    std::string  name_      = "";         // editor display name
    bool         modified_ = true;      // set when local change occurs

    // ───────── Local-space TRS ─────────
    glm::vec3    position_  = glm::vec3(0.0f);
    glm::quat    rotation_  = glm::identity<glm::quat>();
    glm::vec3    euler_angles_ = glm::vec3(0.0f);   // convenience cache
    glm::vec3    scale_     = glm::vec3(1.0f);

    // ───────── Hierarchy ─────────
    Entity               parent_ = entt::null;
    std::vector<Entity>  children_;
    uint32_t             depth_   = 0;            // root = 0

    // ───────── Matrix cache (world-space) ─────────
    glm::mat4 model_  = glm::mat4(1.0f);  // T * R * S
    glm::mat4 normal_ = glm::mat4(1.0f);  // inverse-transpose of 3×3(model)
};

#endif  // TRANSFORM_COMPONENT_H