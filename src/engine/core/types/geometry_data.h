#ifndef GEOMETRY_DATA_H
#define GEOMETRY_DATA_H

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace Geometry {

// ! Rename file to mesh_data.h?
// ! move file to engine/geometry?

// Interleaved VertexData
struct VertexData {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 uv;
  glm::vec3 tangent;
  glm::vec3 bitangent;

  VertexData() : position(), normal(), uv(), tangent(), bitangent() {}

  VertexData(glm::vec3 position, glm::vec3 normal, glm::vec2 uv,
             glm::vec3 tangent, glm::vec3 bitangent)
      : position(position),
        normal(normal),
        uv(uv),
        tangent(tangent),
        bitangent(bitangent) {}
};

struct MeshData {
  std::vector<VertexData> vertices;
  std::vector<uint32_t> indices;
  uint32_t material_index;

  // Skinning, when the mesh has bones: per vertex four bone slots and their
  // weights; each bone a node of the scene's skeleton and its inverse bind
  // matrix
  std::vector<glm::u8vec4> joints;
  std::vector<glm::vec4> weights;
  std::vector<int> bone_nodes;
  std::vector<glm::mat4> bone_offsets;

  size_t VertexCount() const { return vertices.size(); }
  size_t TriangleCount() const { return indices.size() / 3; }
};

}  // namespace Geometry

#endif  // GEOMETRY_DATA_H