#include "region_mesher.h"

#include <unordered_map>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/hash.hpp>

namespace ProcWorld {

const bgfx::VertexLayout& RegionMeshData::Layout() {
  static bgfx::VertexLayout layout;
  if (layout.getStride() == 0) {
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord2, 3, bgfx::AttribType::Float)
        .end();
  }
  return layout;
}

static_assert(sizeof(RegionVertex) == 16 * sizeof(float),
              "RegionVertex has unexpected padding");

// Appends the triangle wound counter-clockwise around `facing` (as the
// primitives are)
static void _AddTriangle(RegionMeshData& mesh, uint32_t a, uint32_t b,
                         uint32_t c, const glm::vec3& facing) {
  const glm::vec3 face =
      glm::cross(mesh.vertices[b].position - mesh.vertices[a].position,
                 mesh.vertices[c].position - mesh.vertices[a].position);
  if (glm::dot(face, facing) < 0.0f)
    std::swap(b, c);
  mesh.indices.insert(mesh.indices.end(), {a, b, c});
}

RegionMeshData RegionMesher::Generate() const {
  using Worldgen::Polygonizer::FromHalf;

  RegionMeshData mesh;
  mesh.vertices.reserve(vertices_.size());
  mesh.indices.reserve(vertices_.size());

  for (const Worldgen::TerrainVertex& source : vertices_) {
    RegionVertex v;
    Worldgen::Polygonizer::DecodeVertex(source, glm::value_ptr(v.position),
                                        glm::value_ptr(v.normal));
    v.tiles = glm::vec4(FromHalf(source.tiles[0]), FromHalf(source.tiles[1]),
                        FromHalf(source.blend),
                        (source.flags & 0x8000) ? 1.0f : 0.0f);
    v.centre_position = origin_ + v.position;
    mesh.vertices.push_back(v);
  }

  const uint32_t count = static_cast<uint32_t>(mesh.vertices.size()) / 3 * 3;
  for (uint32_t i = 0; i < count; i += 3) {
    const auto& v = mesh.vertices;
    _AddTriangle(mesh, i, i + 1, i + 2,
                 v[i].normal + v[i + 1].normal + v[i + 2].normal);
  }

  // Vertices by position: the mesh is a triangle list, each corner its own
  std::unordered_map<glm::vec3, uint32_t> ids;
  auto id = [&](uint32_t vertex) {
    return ids
        .emplace(mesh.vertices[vertex].position,
                 static_cast<uint32_t>(ids.size()))
        .first->second;
  };

  // Skirts: the edges only one triangle uses lie on the region's border;
  // each hangs a strip down toward the planet's centre, hiding the cracks
  // where regions of different levels meet
  if (skirt_depth_ > 0.0f) {
    std::unordered_map<uint64_t, int> uses;
    auto edge_key = [](uint32_t a, uint32_t b) {
      return a < b ? uint64_t(a) << 32 | b : uint64_t(b) << 32 | a;
    };
    for (uint32_t i = 0; i < count; i++)
      uses[edge_key(id(i), id(i - i % 3 + (i + 1) % 3))]++;

    for (uint32_t t = 0; t < count; t += 3) {
      for (uint32_t k = 0; k < 3; k++) {
        const uint32_t a = t + k, b = t + (k + 1) % 3, c = t + (k + 2) % 3;
        if (uses[edge_key(id(a), id(b))] != 1)
          continue;
        const RegionVertex va = mesh.vertices[a];
        const RegionVertex vb = mesh.vertices[b];
        const glm::vec3 edge = vb.position - va.position;
        glm::vec3 out = (va.position + vb.position) * 0.5f -
                        mesh.vertices[c].position;
        out -= edge * glm::dot(out, edge) / glm::dot(edge, edge);

        auto lowered = [&](RegionVertex v) {
          const glm::vec3 down =
              -glm::normalize(v.centre_position) * skirt_depth_;
          v.position += down;
          v.centre_position += down;
          return v;
        };
        const uint32_t first = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(va);
        mesh.vertices.push_back(vb);
        mesh.vertices.push_back(lowered(va));
        mesh.vertices.push_back(lowered(vb));
        _AddTriangle(mesh, first, first + 1, first + 2, out);
        _AddTriangle(mesh, first + 1, first + 3, first + 2, out);
      }
    }
  }

  // Smoothed normals: the mean of the normals sharing a position
  std::unordered_map<glm::vec3, glm::vec3> sums;
  for (const RegionVertex& v : mesh.vertices)
    sums[v.position] += v.normal;
  for (RegionVertex& v : mesh.vertices) {
    const glm::vec3& sum = sums[v.position];
    v.smooth_normal = glm::length(sum) > 0.0001f ? glm::normalize(sum) : v.normal;
  }
  return mesh;
}

}  // namespace ProcWorld
