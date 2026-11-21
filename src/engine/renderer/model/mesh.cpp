#include "mesh.h"

#include <cstring>
#include <unordered_map>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/hash.hpp>

#include "engine/core/logger.h"

bgfx::VertexLayout Mesh::layout_;

// GPU vertex: source vertex + smoothed normal for outline extrusion
struct MeshVertex {
  Geometry::VertexData vertex;
  glm::vec3 smooth_normal;
};

// Averages the normals of all vertices sharing a position, so vertices split
// at hard edges and UV seams get one shared normal. Extruding along these keeps
// outlines closed. (Smooth mesh outlines, solution 2: duplicates are merged by
// position and the smoothed normal is stored in an extra vertex channel)
static std::vector<MeshVertex> BuildVertices(const Geometry::MeshData& src) {
  std::unordered_map<glm::vec3, glm::vec3> normal_sums;
  normal_sums.reserve(src.vertices.size());
  for (const auto& vertex : src.vertices)
    normal_sums[vertex.position] += vertex.normal;

  std::vector<MeshVertex> vertices;
  vertices.reserve(src.vertices.size());
  for (const auto& vertex : src.vertices) {
    const glm::vec3& sum = normal_sums[vertex.position];
    // Opposing normals (e.g. double-sided planes) cancel out: keep the own one
    vertices.push_back(
        {vertex, glm::length(sum) > 0.0001f ? glm::normalize(sum)
                                            : vertex.normal});
  }
  return vertices;
}

void Mesh::EnsureLayout() {
  if (layout_.getStride() != 0)
    return;

  // ? Color0 has been removed
  layout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Tangent, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Bitangent, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord1, 3, bgfx::AttribType::Float)  // smoothed
      .end();
}

Mesh::Mesh(const Geometry::MeshData& src) {
  EnsureLayout();

  static_assert(sizeof(MeshVertex) == (3 + 3 + 2 + 3 + 3 + 3) * sizeof(float),
                "MeshVertex has unexpected padding");

  const std::vector<MeshVertex> vertices = BuildVertices(src);
  const bgfx::Memory* vmem =
      bgfx::copy(vertices.data(), sizeof(MeshVertex) * vertices.size());
  vbo_ = bgfx::createVertexBuffer(vmem, layout_);

  const bgfx::Memory* imem =
      bgfx::copy(src.indices.data(), sizeof(uint32_t) * src.indices.size());
  ibo_ = bgfx::createIndexBuffer(imem, BGFX_BUFFER_INDEX32);

  index_count_ = static_cast<uint32_t>(src.indices.size());
  material_index_ = src.material_index;
  SetBounds(reinterpret_cast<const uint8_t*>(src.vertices.data()),
            static_cast<uint32_t>(src.vertices.size()),
            sizeof(Geometry::VertexData));
}

Mesh::Mesh(const void* vertices, uint32_t vertex_count,
           const bgfx::VertexLayout& layout,
           const std::vector<uint32_t>& indices) {
  vbo_ = bgfx::createVertexBuffer(
      bgfx::copy(vertices, layout.getSize(vertex_count)), layout);
  ibo_ = bgfx::createIndexBuffer(
      bgfx::copy(indices.data(), sizeof(uint32_t) * indices.size()),
      BGFX_BUFFER_INDEX32);
  index_count_ = static_cast<uint32_t>(indices.size());
  SetBounds(static_cast<const uint8_t*>(vertices) +
                layout.getOffset(bgfx::Attrib::Position),
            vertex_count, layout.getStride());
}

// Box center, and the farthest vertex from it
void Mesh::SetBounds(const uint8_t* positions, uint32_t count,
                     uint32_t stride) {
  if (count == 0)
    return;
  const auto position = [&](uint32_t i) {
    glm::vec3 p;
    std::memcpy(&p, positions + size_t(i) * stride, sizeof(p));
    return p;
  };
  glm::vec3 low = position(0), high = low;
  for (uint32_t i = 1; i < count; ++i) {
    low = glm::min(low, position(i));
    high = glm::max(high, position(i));
  }
  bounds_center_ = (low + high) * 0.5f;
  float radius2 = 0.0f;
  for (uint32_t i = 0; i < count; ++i) {
    const glm::vec3 d = position(i) - bounds_center_;
    radius2 = glm::max(radius2, glm::dot(d, d));
  }
  bounds_radius_ = glm::sqrt(radius2);
}

Mesh::~Mesh() {
  if (bgfx::isValid(vbo_))
    bgfx::destroy(vbo_);
  if (bgfx::isValid(ibo_))
    bgfx::destroy(ibo_);
}

void Mesh::Bind() const {
  bgfx::setVertexBuffer(0, vbo_);
  bgfx::setIndexBuffer(ibo_);
}

uint32_t Mesh::MaterialIndex() const {
  return material_index_;
}
