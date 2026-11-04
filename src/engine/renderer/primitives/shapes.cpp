#include "shapes.h"

#include <cmath>
#include <vector>

#include <glm/glm.hpp>

namespace Shapes {

using Geometry::MeshData;
using Geometry::VertexData;

// Triangles are wound so cross(b - a, c - a) points along the face normal
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr uint32_t kSegments = 32;

// Point of a revolved profile: distance from the y axis, height and the
// normal's (outward, up) components
struct ProfilePoint {
  float radius, y;
  float normal_out, normal_up;
};

// Sweeps a top-to-bottom profile around the y axis. Repeating a point with a
// different normal gives a hard edge
MeshData Revolve(const std::vector<ProfilePoint>& profile) {
  MeshData mesh;
  mesh.material_index = 0;

  const uint32_t rows = static_cast<uint32_t>(profile.size());
  for (uint32_t j = 0; j < rows; ++j) {
    const ProfilePoint& p = profile[j];
    for (uint32_t i = 0; i <= kSegments; ++i) {
      const float u = float(i) / kSegments;
      const float angle = u * 2.0f * kPi;
      const glm::vec3 out(std::cos(angle), 0.0f, std::sin(angle));

      const glm::vec3 normal = glm::normalize(
          out * p.normal_out + glm::vec3(0.0f, p.normal_up, 0.0f));
      const glm::vec3 tangent(-std::sin(angle), 0.0f, std::cos(angle));
      mesh.vertices.emplace_back(
          out * p.radius + glm::vec3(0.0f, p.y, 0.0f), normal,
          glm::vec2(u, float(j) / (rows - 1)), tangent,
          glm::cross(normal, tangent));
    }
  }

  for (uint32_t j = 0; j + 1 < rows; ++j) {
    for (uint32_t i = 0; i < kSegments; ++i) {
      const uint32_t a = j * (kSegments + 1) + i, b = a + 1;
      const uint32_t c = a + kSegments + 1, d = c + 1;
      mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
    }
  }
  return mesh;
}

// Rings of a sphere of given radius from latitude phi_from to phi_to (0 = top)
void AddSphereRings(std::vector<ProfilePoint>& profile, float radius,
                    float y_offset, float phi_from, float phi_to,
                    uint32_t rings) {
  for (uint32_t k = 0; k <= rings; ++k) {
    const float phi = phi_from + (phi_to - phi_from) * float(k) / rings;
    profile.push_back({radius * std::sin(phi),
                       y_offset + radius * std::cos(phi), std::sin(phi),
                       std::cos(phi)});
  }
}

}  // namespace

MeshData PlaneData() {
  MeshData mesh;
  mesh.material_index = 0;
  mesh.vertices = {
      VertexData(glm::vec3(-1, 0, -1), glm::vec3(0, 1, 0), glm::vec2(0, 1), glm::vec3(1, 0, 0), glm::vec3(0, 0, -1)),
      VertexData(glm::vec3(1, 0, -1), glm::vec3(0, 1, 0), glm::vec2(1, 1), glm::vec3(1, 0, 0), glm::vec3(0, 0, -1)),
      VertexData(glm::vec3(-1, 0, 1), glm::vec3(0, 1, 0), glm::vec2(0, 0), glm::vec3(1, 0, 0), glm::vec3(0, 0, -1)),
      VertexData(glm::vec3(1, 0, 1), glm::vec3(0, 1, 0), glm::vec2(1, 0), glm::vec3(1, 0, 0), glm::vec3(0, 0, -1))};

  mesh.indices = {
      0, 2, 1,
      1, 2, 3};
  return mesh;
}

MeshData CubeData() {
  MeshData mesh;
  mesh.material_index = 0;
  mesh.vertices = {
      // Front Face
      VertexData(glm::vec3(-1, -1, 1), glm::vec3(0, 0, 1), glm::vec2(0, 0), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0)),
      VertexData(glm::vec3(1, -1, 1), glm::vec3(0, 0, 1), glm::vec2(1, 0), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0)),
      VertexData(glm::vec3(1, 1, 1), glm::vec3(0, 0, 1), glm::vec2(1, 1), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0)),
      VertexData(glm::vec3(-1, 1, 1), glm::vec3(0, 0, 1), glm::vec2(0, 1), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0)),

      // Back Face
      VertexData(glm::vec3(1, -1, -1), glm::vec3(0, 0, -1), glm::vec2(0, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0)),
      VertexData(glm::vec3(-1, -1, -1), glm::vec3(0, 0, -1), glm::vec2(1, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0)),
      VertexData(glm::vec3(-1, 1, -1), glm::vec3(0, 0, -1), glm::vec2(1, 1), glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0)),
      VertexData(glm::vec3(1, 1, -1), glm::vec3(0, 0, -1), glm::vec2(0, 1), glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0)),

      // Left Face
      VertexData(glm::vec3(-1, -1, -1), glm::vec3(-1, 0, 0), glm::vec2(0, 0), glm::vec3(0, 0, 1), glm::vec3(0, 1, 0)),
      VertexData(glm::vec3(-1, -1, 1), glm::vec3(-1, 0, 0), glm::vec2(1, 0), glm::vec3(0, 0, 1), glm::vec3(0, 1, 0)),
      VertexData(glm::vec3(-1, 1, 1), glm::vec3(-1, 0, 0), glm::vec2(1, 1), glm::vec3(0, 0, 1), glm::vec3(0, 1, 0)),
      VertexData(glm::vec3(-1, 1, -1), glm::vec3(-1, 0, 0), glm::vec2(0, 1), glm::vec3(0, 0, 1), glm::vec3(0, 1, 0)),

      // Right Face
      VertexData(glm::vec3(1, -1, 1), glm::vec3(1, 0, 0), glm::vec2(0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0)),
      VertexData(glm::vec3(1, -1, -1), glm::vec3(1, 0, 0), glm::vec2(1, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0)),
      VertexData(glm::vec3(1, 1, -1), glm::vec3(1, 0, 0), glm::vec2(1, 1), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0)),
      VertexData(glm::vec3(1, 1, 1), glm::vec3(1, 0, 0), glm::vec2(0, 1), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0)),

      // Bottom Face
      VertexData(glm::vec3(-1, -1, -1), glm::vec3(0, -1, 0), glm::vec2(0, 0), glm::vec3(1, 0, 0), glm::vec3(0, 0, -1)),
      VertexData(glm::vec3(1, -1, -1), glm::vec3(0, -1, 0), glm::vec2(1, 0), glm::vec3(1, 0, 0), glm::vec3(0, 0, -1)),
      VertexData(glm::vec3(1, -1, 1), glm::vec3(0, -1, 0), glm::vec2(1, 1), glm::vec3(1, 0, 0), glm::vec3(0, 0, -1)),
      VertexData(glm::vec3(-1, -1, 1), glm::vec3(0, -1, 0), glm::vec2(0, 1), glm::vec3(1, 0, 0), glm::vec3(0, 0, -1)),

      // Top Face
      VertexData(glm::vec3(-1, 1, 1), glm::vec3(0, 1, 0), glm::vec2(0, 0), glm::vec3(1, 0, 0), glm::vec3(0, 0, 1)),
      VertexData(glm::vec3(1, 1, 1), glm::vec3(0, 1, 0), glm::vec2(1, 0), glm::vec3(1, 0, 0), glm::vec3(0, 0, 1)),
      VertexData(glm::vec3(1, 1, -1), glm::vec3(0, 1, 0), glm::vec2(1, 1), glm::vec3(1, 0, 0), glm::vec3(0, 0, 1)),
      VertexData(glm::vec3(-1, 1, -1), glm::vec3(0, 1, 0), glm::vec2(0, 1), glm::vec3(1, 0, 0), glm::vec3(0, 0, 1))};

  mesh.indices = {
      // Front Face
      0, 1, 2,  0, 2, 3,
      // Back Face
      4, 5, 6,  4, 6, 7,
      // Left Face
      8, 9, 10,  8, 10, 11,
      // Right Face
      12, 13, 14,  12, 14, 15,
      // Bottom Face
      16, 17, 18,  16, 18, 19,
      // Top Face
      20, 21, 22,  20, 22, 23};
  return mesh;
}

MeshData SphereData() {
  std::vector<ProfilePoint> profile;
  AddSphereRings(profile, 1.0f, 0.0f, 0.0f, kPi, kSegments / 2);
  return Revolve(profile);
}

MeshData CylinderData() {
  // Top cap, side, bottom cap; each edge repeated for its own normal
  return Revolve({{0, 1, 0, 1},
                  {1, 1, 0, 1},
                  {1, 1, 1, 0},
                  {1, -1, 1, 0},
                  {1, -1, 0, -1},
                  {0, -1, 0, -1}});
}

MeshData CapsuleData() {
  // Radius 0.5 hemispheres around a 1-unit side; the side is the band
  // between the two equators
  std::vector<ProfilePoint> profile;
  AddSphereRings(profile, 0.5f, 0.5f, 0.0f, kPi * 0.5f, kSegments / 4);
  AddSphereRings(profile, 0.5f, -0.5f, kPi * 0.5f, kPi, kSegments / 4);
  return Revolve(profile);
}

MeshData PyramidData() {
  MeshData mesh;
  mesh.material_index = 0;

  // Base corners counter-clockwise seen from above (x, z)
  const glm::vec3 apex(0, 1, 0);
  const glm::vec3 base[4] = {
      {-1, -1, 1}, {1, -1, 1}, {1, -1, -1}, {-1, -1, -1}};

  // Side faces
  for (uint32_t i = 0; i < 4; ++i) {
    const glm::vec3 a = base[i], b = base[(i + 1) % 4];
    const glm::vec3 normal = glm::normalize(glm::cross(b - a, apex - a));
    const glm::vec3 tangent = glm::normalize(b - a);
    const glm::vec3 bitangent = glm::cross(normal, tangent);
    const uint32_t first = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.emplace_back(a, normal, glm::vec2(0, 0), tangent, bitangent);
    mesh.vertices.emplace_back(b, normal, glm::vec2(1, 0), tangent, bitangent);
    mesh.vertices.emplace_back(apex, normal, glm::vec2(0.5f, 1), tangent,
                               bitangent);
    mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2});
  }

  // Bottom face
  const uint32_t first = static_cast<uint32_t>(mesh.vertices.size());
  mesh.vertices.insert(mesh.vertices.end(), {
      VertexData(glm::vec3(-1, -1, -1), glm::vec3(0, -1, 0), glm::vec2(0, 0), glm::vec3(1, 0, 0), glm::vec3(0, 0, -1)),
      VertexData(glm::vec3(1, -1, -1), glm::vec3(0, -1, 0), glm::vec2(1, 0), glm::vec3(1, 0, 0), glm::vec3(0, 0, -1)),
      VertexData(glm::vec3(1, -1, 1), glm::vec3(0, -1, 0), glm::vec2(1, 1), glm::vec3(1, 0, 0), glm::vec3(0, 0, -1)),
      VertexData(glm::vec3(-1, -1, 1), glm::vec3(0, -1, 0), glm::vec2(0, 1), glm::vec3(1, 0, 0), glm::vec3(0, 0, -1))});
  mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2,
                                           first, first + 2, first + 3});
  return mesh;
}

Mesh* CreatePlane() { return new Mesh(PlaneData()); }
Mesh* CreateCube() { return new Mesh(CubeData()); }
Mesh* CreateSphere() { return new Mesh(SphereData()); }
Mesh* CreateCylinder() { return new Mesh(CylinderData()); }
Mesh* CreateCapsule() { return new Mesh(CapsuleData()); }
Mesh* CreatePyramid() { return new Mesh(PyramidData()); }

}  // namespace Shapes
