#ifndef REGION_MESHER_H
#define REGION_MESHER_H

#include <glm/glm.hpp>
#include <vector>

#include "engine/geometry/meshing/mesher_base.h"

#include "terrain/polygonizer.h"

namespace ProcWorld {

// A worldgen region's terrain (half-float triangle list) as mesh data,
// relative to the region origin. With a skirt depth, the region's border
// edges get a strip that far down toward the planet's centre (the region
// origin is then needed, in planet space)
class RegionMesher : public Geometry::MesherBase {
public:
  explicit RegionMesher(const std::vector<Worldgen::TerrainVertex>& vertices,
                        const glm::vec3& origin = glm::vec3(0.0f),
                        float skirt_depth = 0.0f)
      : vertices_(vertices), origin_(origin), skirt_depth_(skirt_depth) {}

  Geometry::MeshData Generate() const override;

private:
  const std::vector<Worldgen::TerrainVertex>& vertices_;
  glm::vec3 origin_;
  float skirt_depth_;
};

}  // namespace ProcWorld

#endif  // REGION_MESHER_H
