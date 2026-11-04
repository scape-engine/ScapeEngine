#ifndef SHAPES_H
#define SHAPES_H

#include "engine/core/types/geometry_data.h"
#include "engine/renderer/model/mesh.h"

// Built-in shapes spanning -1..1, y up. The caller owns the returned mesh
namespace Shapes {

Mesh* CreatePlane();
Mesh* CreateCube();
Mesh* CreateSphere();
Mesh* CreateCylinder();
Mesh* CreateCapsule();
Mesh* CreatePyramid();

// Geometry the Create functions upload
Geometry::MeshData PlaneData();
Geometry::MeshData CubeData();
Geometry::MeshData SphereData();
Geometry::MeshData CylinderData();
Geometry::MeshData CapsuleData();
Geometry::MeshData PyramidData();

}  // namespace Shapes

#endif  // SHAPES_H
