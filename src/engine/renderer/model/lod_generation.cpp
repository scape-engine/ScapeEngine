#include "lod_generation.h"

#include <algorithm>

#include <meshoptimizer.h>

namespace LodGeneration {

// How far a simplified surface may move, relative to the mesh's size
constexpr float max_error = 0.1f;

Geometry::MeshData Simplify(const Geometry::MeshData& mesh, float detail) {
  Geometry::MeshData out = mesh;
  const size_t count = mesh.indices.size();
  const size_t target = std::max<size_t>(
      3, static_cast<size_t>(static_cast<float>(count / 3) *
                             std::clamp(detail, 0.0f, 1.0f)) *
             3);
  if (count < 6 || target >= count)
    return out;
  const float* positions = &mesh.vertices[0].position.x;
  const size_t stride = sizeof(Geometry::VertexData);
  out.indices.resize(meshopt_simplify(
      out.indices.data(), mesh.indices.data(), count, positions,
      mesh.vertices.size(), stride, target, max_error, 0, nullptr));
  // Seams keep the topology-preserving pass from getting near the target
  if (out.indices.size() > target + target / 4) {
    out.indices.resize(count);
    out.indices.resize(meshopt_simplifySloppy(
        out.indices.data(), mesh.indices.data(), count, positions,
        mesh.vertices.size(), stride, target, max_error, nullptr));
  }
  if (out.indices.empty())
    return mesh;
  // TODO: Compact the unused vertices (meshopt_optimizeVertexFetch with the
  // skinning arrays remapped alike) if LOD memory matters
  return out;
}

}  // namespace LodGeneration
