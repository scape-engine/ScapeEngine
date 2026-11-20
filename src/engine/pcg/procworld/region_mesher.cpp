#include "region_mesher.h"

#include <unordered_map>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/hash.hpp>

namespace ProcWorld {

// Appends the triangle wound counter-clockwise around `facing` (as the
// primitives are)
static void _AddTriangle(Geometry::MeshData& mesh, uint32_t a, uint32_t b,
                         uint32_t c, const glm::vec3& facing) {
  const glm::vec3 face =
      glm::cross(mesh.vertices[b].position - mesh.vertices[a].position,
                 mesh.vertices[c].position - mesh.vertices[a].position);
  if (glm::dot(face, facing) < 0.0f)
    std::swap(b, c);
  mesh.indices.insert(mesh.indices.end(), {a, b, c});
}

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

  const uint32_t count = static_cast<uint32_t>(mesh.vertices.size()) / 3 * 3;
  for (uint32_t i = 0; i < count; i += 3) {
    const auto& v = mesh.vertices;
    _AddTriangle(mesh, i, i + 1, i + 2,
                 v[i].normal + v[i + 1].normal + v[i + 2].normal);
  }
  if (skirt_depth_ <= 0.0f)
    return mesh;

  // Skirts: the edges only one triangle uses lie on the region's border;
  // each hangs a strip down toward the planet's centre, hiding the cracks
  // where regions of different levels meet
  std::unordered_map<glm::vec3, uint32_t> ids;  // vertices by position
  auto id = [&](uint32_t vertex) {
    return ids
        .emplace(mesh.vertices[vertex].position,
                 static_cast<uint32_t>(ids.size()))
        .first->second;
  };
  std::unordered_map<uint64_t, int> uses;
  auto edge_key = [](uint32_t a, uint32_t b) {
    return a < b ? uint64_t(a) << 32 | b : uint64_t(b) << 32 | a;
  };
  for (uint32_t i = 0; i < count; i++)
    uses[edge_key(id(i), id(i - i % 3 + (i + 1) % 3))]++;

  for (uint32_t t = 0; t < count; t += 3) {
    for (uint32_t k = 0; k < 3; k++) {
      const uint32_t a = t + k, b = t + (k + 1) % 3, c = t + (k + 2) % 3;
      if (uses[edge_key(id(a), id(b))] != 1)
        continue;
      const Geometry::VertexData va = mesh.vertices[a];
      const Geometry::VertexData vb = mesh.vertices[b];
      const glm::vec3 edge = vb.position - va.position;
      glm::vec3 out = (va.position + vb.position) * 0.5f -
                      mesh.vertices[c].position;
      out -= edge * glm::dot(out, edge) / glm::dot(edge, edge);

      auto lowered = [&](Geometry::VertexData v) {
        v.position -= glm::normalize(origin_ + v.position) * skirt_depth_;
        return v;
      };
      const uint32_t first = static_cast<uint32_t>(mesh.vertices.size());
      mesh.vertices.push_back(va);
      mesh.vertices.push_back(vb);
      mesh.vertices.push_back(lowered(va));
      mesh.vertices.push_back(lowered(vb));
      _AddTriangle(mesh, first, first + 1, first + 2, out);
      _AddTriangle(mesh, first + 1, first + 3, first + 2, out);
    }
  }
  return mesh;
}

}  // namespace ProcWorld
