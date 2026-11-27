#ifndef HEIGHTMAP_MESHER_H
#define HEIGHTMAP_MESHER_H

#include <functional>
#include <glm/glm.hpp>

#include "engine/core/types/geometry_data.h"

namespace Geometry {

struct HeightSample {
  float height;
  float slope;
  glm::vec3 color;
};

using HeightSampleFn = std::function<HeightSample(float x, float y)>;

struct MesherParams {
  uint16_t size = 256;
  // Where sample (0, 0) sits on the xz plane, and how far apart samples are
  glm::vec2 origin{0.0f};
  glm::vec2 spacing{1.0f};
  bool with_normals = true;
  bool with_colors = true;
  bool with_uvs = true;
};

class HeightmapMesher {
public:
  HeightmapMesher(HeightSampleFn sample_fn, const MesherParams& params);
  MeshData Generate() const;

private:
  HeightSampleFn sample_fn_;
  MesherParams params_;
};

}  // namespace Geometry

#endif  // HEIGHTMAP_MESHER_H