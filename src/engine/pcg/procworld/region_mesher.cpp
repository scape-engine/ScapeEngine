#include "region_mesher.h"

#include <glm/gtc/type_ptr.hpp>

namespace ProcWorld {

Geometry::MeshData RegionMesher::Generate() const {
  Geometry::MeshData mesh;
  mesh.material_index = 0;
  mesh.vertices.reserve(vertices_.size());
  mesh.indices.reserve(vertices_.size());

  for (const Worldgen::TerrainVertex& source : vertices_) {
    glm::vec3 position, normal;
    Worldgen::Polygonizer::DecodeVertex(source, glm::value_ptr(position),
                                        glm::value_ptr(normal));

    // Any axis across the normal: the terrain has no normal map yet
    const glm::vec3 axis = glm::abs(normal.y) < 0.99f ? glm::vec3(0, 1, 0)
                                                      : glm::vec3(1, 0, 0);
    const glm::vec3 tangent = glm::normalize(glm::cross(axis, normal));
    mesh.vertices.emplace_back(position, normal,
                               glm::vec2(position.x, position.z), tangent,
                               glm::cross(normal, tangent));
  }

  // Front faces wind counter-clockwise around their normal (as the
  // primitives do); turn the triangles that face the other way
  const uint32_t count = static_cast<uint32_t>(mesh.vertices.size()) / 3 * 3;
  for (uint32_t i = 0; i < count; i += 3) {
    const auto& a = mesh.vertices[i];
    const auto& b = mesh.vertices[i + 1];
    const auto& c = mesh.vertices[i + 2];
    const glm::vec3 face =
        glm::cross(b.position - a.position, c.position - a.position);
    if (glm::dot(face, a.normal + b.normal + c.normal) < 0.0f)
      mesh.indices.insert(mesh.indices.end(), {i, i + 2, i + 1});
    else
      mesh.indices.insert(mesh.indices.end(), {i, i + 1, i + 2});
  }
  return mesh;
}

}  // namespace ProcWorld
