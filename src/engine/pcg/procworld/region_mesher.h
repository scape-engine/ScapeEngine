#ifndef REGION_MESHER_H
#define REGION_MESHER_H

#include <vector>

#include "engine/geometry/meshing/mesher_base.h"

#include "terrain/polygonizer.h"

namespace ProcWorld {

// A worldgen region's terrain (half-float triangle list) as mesh data,
// relative to the region origin
class RegionMesher : public Geometry::MesherBase {
public:
  explicit RegionMesher(const std::vector<Worldgen::TerrainVertex>& vertices)
      : vertices_(vertices) {}

  Geometry::MeshData Generate() const override;

private:
  const std::vector<Worldgen::TerrainVertex>& vertices_;
};

}  // namespace ProcWorld

#endif  // REGION_MESHER_H
