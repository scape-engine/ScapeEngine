#ifndef REGION_MESHER_H
#define REGION_MESHER_H

#include <bgfx/bgfx.h>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

#include "terrain/polygonizer.h"

namespace ProcWorld {

// A terrain vertex as TerrainMaterial reads it
struct RegionVertex {
  glm::vec3 position;  // region-local
  glm::vec3 normal;
  glm::vec4 tiles;            // two tile groups, blend, 1 below sea level
  glm::vec3 smooth_normal;    // for the selection outline
  glm::vec3 centre_position;  // relative to the planet's centre
};

struct RegionMeshData {
  std::vector<RegionVertex> vertices;
  std::vector<uint32_t> indices;

  static const bgfx::VertexLayout& Layout();
};

// A worldgen region's terrain (half-float triangle list) in the terrain
// layout, relative to the region origin (planet space). With a skirt depth,
// the region's border edges get a strip that far down toward the planet's
// centre.
// Not a Geometry::MesherBase: its vertices are not Geometry::VertexData
class RegionMesher {
public:
  RegionMesher(const std::vector<Worldgen::TerrainVertex>& vertices,
               const glm::vec3& origin, float skirt_depth = 0.0f)
      : vertices_(vertices), origin_(origin), skirt_depth_(skirt_depth) {}

  RegionMeshData Generate() const;

private:
  const std::vector<Worldgen::TerrainVertex>& vertices_;
  glm::vec3 origin_;
  float skirt_depth_;
};

}  // namespace ProcWorld

#endif  // REGION_MESHER_H
