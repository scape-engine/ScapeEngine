#ifndef LOD_GENERATION_H
#define LOD_GENERATION_H

#include "engine/core/types/geometry_data.h"

namespace LodGeneration {

// A coarser copy of the mesh keeping about `detail` (0..1) of its
// triangles, for a generated LOD. Shapes are kept where the mesh's topology
// allows; meshes split into many pieces (hard edges, seams) are reduced by
// position alone. Vertices, materials and skinning stay as they are
Geometry::MeshData Simplify(const Geometry::MeshData& mesh, float detail);

}  // namespace LodGeneration

#endif  // LOD_GENERATION_H
