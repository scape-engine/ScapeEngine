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

  if (!src.bone_nodes.empty() && src.bone_nodes.size() <= max_bones &&
      src.joints.size() == src.vertices.size()) {
    static bgfx::VertexLayout skin_layout;
    if (skin_layout.getStride() == 0)
      skin_layout.begin()
          // floats: integer attributes don't reach a float input alike on
          // every backend
          .add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Float)
          .add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Float)
          .end();
    std::vector<uint8_t> skin(skin_layout.getSize(
        static_cast<uint32_t>(src.vertices.size())));
    for (size_t i = 0; i < src.vertices.size(); ++i) {
      uint8_t* at = skin.data() + i * skin_layout.getStride();
      const glm::vec4 joints(src.joints[i]);
      std::memcpy(at, &joints, sizeof(glm::vec4));
      std::memcpy(at + sizeof(glm::vec4), &src.weights[i], sizeof(glm::vec4));
    }
    skin_vbo_ = bgfx::createVertexBuffer(
        bgfx::copy(skin.data(), static_cast<uint32_t>(skin.size())),
        skin_layout);
    bone_nodes_ = src.bone_nodes;
    bone_offsets_ = src.bone_offsets;
  }

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
  if (bgfx::isValid(skin_vbo_))
    bgfx::destroy(skin_vbo_);
}

void Mesh::Bind() const {
  bgfx::setVertexBuffer(0, vbo_);
  if (bgfx::isValid(skin_vbo_))
    bgfx::setVertexBuffer(1, skin_vbo_);
  bgfx::setIndexBuffer(ibo_);
}

void Mesh::SkinMatrices(const glm::mat4* pose, uint32_t count,
                        glm::mat4* out) const {
  // A bone naming no node of the pose stays in the bind pose
  for (size_t i = 0; i < bone_nodes_.size(); ++i)
    out[i] = bone_nodes_[i] < 0 || bone_nodes_[i] >= static_cast<int>(count)
                 ? glm::mat4(1.0f)
                 : pose[bone_nodes_[i]] * bone_offsets_[i];
}

uint32_t Mesh::MaterialIndex() const {
  return material_index_;
}

int Mesh::ForDistance(float distance, Drawn out[2]) const {
  // Its levels: itself from 0 on, then each LOD with a mesh from its distance
  const auto next_of = [&](size_t i) {
    while (i < lods_.size() && !lods_[i].mesh)
      ++i;
    return i;
  };
  // A level from `start` on, until the LOD at `next` (none past the last)
  const auto level = [&](const Mesh* mesh, float start, size_t next) {
    glm::vec4 lod(0.0f);
    if (start > 0.0f) {
      lod.x = start * (1.0f - lod_fade_);
      lod.y = start;
    }
    if (next < lods_.size()) {
      lod.z = lods_[next].distance * (1.0f - lod_fade_);
      lod.w = lods_[next].distance;
    }
    return Drawn{mesh, lod};
  };
  size_t at = lods_.size();  // itself
  for (size_t i = 0; i < lods_.size(); ++i)
    if (lods_[i].mesh && distance >= lods_[i].distance)
      at = i;
  const size_t next = next_of(at == lods_.size() ? 0 : at + 1);
  out[0] = at == lods_.size()
               ? level(this, 0.0f, next)
               : level(lods_[at].mesh, lods_[at].distance, next);
  if (next == lods_.size() || lod_fade_ <= 0.0f ||
      distance < lods_[next].distance * (1.0f - lod_fade_))
    return 1;
  out[1] = level(lods_[next].mesh, lods_[next].distance, next_of(next + 1));
  return 2;
}

void Mesh::SetDraw(float fade_start, float fade_end, float lod_fade,
                   std::vector<Lod> lods) {
  fade_start_ = fade_start;
  fade_end_ = fade_end;
  lod_fade_ = lod_fade;
  lods_ = std::move(lods);
}
