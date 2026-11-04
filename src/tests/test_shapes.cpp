// Checks must run in release builds too
#undef NDEBUG
#include <cassert>

#include "engine/renderer/primitives/shapes.h"

#include <iostream>

namespace Tests {

// Every triangle of the convex shapes must face away from the center, or the
// culled passes drop it
void TestShapes() {
  for (auto data : {Shapes::CubeData, Shapes::SphereData, Shapes::CapsuleData,
                    Shapes::CylinderData, Shapes::PyramidData}) {
    const Geometry::MeshData mesh = data();
    assert(!mesh.indices.empty() && mesh.indices.size() % 3 == 0);
    for (size_t t = 0; t < mesh.indices.size(); t += 3) {
      for (int k = 0; k < 3; ++k)
        assert(mesh.indices[t + k] < mesh.vertices.size());
      const glm::vec3 a = mesh.vertices[mesh.indices[t]].position;
      const glm::vec3 b = mesh.vertices[mesh.indices[t + 1]].position;
      const glm::vec3 c = mesh.vertices[mesh.indices[t + 2]].position;
      const glm::vec3 face = glm::cross(b - a, c - a);
      assert(glm::dot(face, a + b + c) >= -1e-6f);
    }
    for (const auto& v : mesh.vertices)
      assert(std::abs(glm::dot(v.tangent, v.normal)) < 1e-3f);
  }
  const Geometry::MeshData plane = Shapes::PlaneData();
  assert(glm::cross(plane.vertices[plane.indices[1]].position -
                        plane.vertices[plane.indices[0]].position,
                    plane.vertices[plane.indices[2]].position -
                        plane.vertices[plane.indices[0]].position)
             .y > 0.0f);
  std::cout << "Shapes: OK" << std::endl;
}

}  // namespace Tests
