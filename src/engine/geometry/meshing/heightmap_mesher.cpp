#include "heightmap_mesher.h"

#include <glm/glm.hpp>

namespace Geometry {

HeightmapMesher::HeightmapMesher(HeightSampleFn sample_fn,
                                 const MesherParams& params)
    : sample_fn_(std::move(sample_fn)), params_(params) {}

MeshData HeightmapMesher::Generate() const {
  MeshData mesh;
  mesh.material_index = 0;

  const uint16_t size = params_.size;
  const size_t vcount = static_cast<size_t>(size) * size;

  mesh.vertices.reserve(vcount);
  mesh.indices.reserve((size - 1) * (size - 1) * 6);

  std::vector<glm::vec3> positions_temp(vcount);
  std::vector<glm::vec3> normals_temp(vcount, glm::vec3(0.0f));
  std::vector<glm::vec2> uvs_temp(vcount, glm::vec2(0.0f));

  // Generate positions and UVs
  for (uint16_t y = 0; y < size; ++y) {
    for (uint16_t x = 0; x < size; ++x) {
      HeightSample s = sample_fn_(static_cast<float>(x), static_cast<float>(y));
      size_t idx = y * size + x;
      const glm::vec2 at =
          params_.origin + glm::vec2(x, y) * params_.spacing;
      positions_temp[idx] = glm::vec3(at.x, s.height, at.y);

      if (params_.with_uvs) {
        uvs_temp[idx] = glm::vec2(x / static_cast<float>(size - 1), s.slope);
      }
    }
  }

  // Generate indices and accumulate face normals
  for (uint16_t y = 0; y < size - 1; ++y) {
    for (uint16_t x = 0; x < size - 1; ++x) {
      uint32_t i = y * size + x;

      mesh.indices.push_back(i);
      mesh.indices.push_back(i + size);
      mesh.indices.push_back(i + 1);

      mesh.indices.push_back(i + 1);
      mesh.indices.push_back(i + size);
      mesh.indices.push_back(i + size + 1);

      if (params_.with_normals) {
        glm::vec3 n1 = glm::normalize(
            glm::cross(positions_temp[i + size] - positions_temp[i],
                       positions_temp[i + 1] - positions_temp[i]));
        normals_temp[i] += n1;
        normals_temp[i + size] += n1;
        normals_temp[i + 1] += n1;

        glm::vec3 n2 = glm::normalize(
            glm::cross(positions_temp[i + size] - positions_temp[i + 1],
                       positions_temp[i + size + 1] - positions_temp[i + 1]));
        normals_temp[i + 1] += n2;
        normals_temp[i + size] += n2;
        normals_temp[i + size + 1] += n2;
      }
    }
  }

  // Build interleaved vertices
  for (size_t i = 0; i < vcount; ++i) {
    VertexData v;
    v.position = positions_temp[i];
    v.normal = params_.with_normals ? glm::normalize(normals_temp[i])
                                    : glm::vec3(0.0f, 1.0f, 0.0f);
    v.uv = uvs_temp[i];
    v.tangent = glm::vec3(0.0f);
    v.bitangent = glm::vec3(0.0f);
    mesh.vertices.push_back(v);
  }

  return mesh;
}

}  // namespace Geometry