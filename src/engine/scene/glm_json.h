#ifndef GLM_JSON_H
#define GLM_JSON_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

namespace nlohmann {
template <>
struct adl_serializer<glm::vec3> {
  static void to_json(json& j, const glm::vec3& v) { j = {v.x, v.y, v.z}; }
  static void from_json(const json& j, glm::vec3& v) {
    v = glm::vec3(j.at(0).get<float>(), j.at(1).get<float>(),
                  j.at(2).get<float>());
  }
};

// Stored as w, x, y, z
template <>
struct adl_serializer<glm::quat> {
  static void to_json(json& j, const glm::quat& q) { j = {q.w, q.x, q.y, q.z}; }
  static void from_json(const json& j, glm::quat& q) {
    q = glm::quat(j.at(0).get<float>(), j.at(1).get<float>(),
                  j.at(2).get<float>(), j.at(3).get<float>());
  }
};
}  // namespace nlohmann

#endif  // GLM_JSON_H
