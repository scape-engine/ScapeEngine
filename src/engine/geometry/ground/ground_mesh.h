#ifndef GROUND_MESH_H
#define GROUND_MESH_H

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

// A triangle list as ground to stand on: where the line through a point
// along `up` meets it. The triangles sit in a grid over the plane across
// `up`, so a query tests the few under the point
class GroundMesh {
 public:
  GroundMesh(std::vector<glm::vec3> triangles, const glm::vec3& up);

  // The ground point on the line through `point` along up nearest `point`
  // (above or below it); false when no triangle is under it
  bool Under(const glm::vec3& point, glm::vec3& hit) const;

  const glm::vec3& Up() const { return up_; }

 private:
  glm::ivec2 Cell(const glm::vec3& point) const;

  std::vector<glm::vec3> triangles_;
  glm::vec3 up_, u_, v_;  // up and the plane's axes
  glm::vec2 low_{0.0f};
  float cell_ = 1.0f;
  int columns_ = 0, rows_ = 0;
  // Each cell's triangles: items_[starts_[c] .. starts_[c + 1])
  std::vector<uint32_t> starts_, items_;
};

#endif  // GROUND_MESH_H
