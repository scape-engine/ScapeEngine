#ifndef MESH_H
#define MESH_H

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <vector>

#include "engine/core/types/geometry_data.h"

class Mesh {
public:
  explicit Mesh(const Geometry::MeshData& src);
  // Vertices in the caller's own layout (e.g. a terrain format). Passes that
  // draw every mesh read position, normal and, for the selection outline, a
  // smoothed normal in TexCoord1: the layout must have them
  Mesh(const void* vertices, uint32_t vertex_count,
       const bgfx::VertexLayout& layout, const std::vector<uint32_t>& indices);
  ~Mesh();

  Mesh(const Mesh&) = delete;
  Mesh& operator=(const Mesh&) = delete;

  // bind VBO/IBO
  void Bind() const;

  uint32_t IndexCount() const { return index_count_; }  // TODO: remove this

  // Return mesh vertex array object
  uint32_t VAO() const;

  // Return mesh vertex buffer object
  uint32_t VBO() const;

  // Return mesh element buffer object
  uint32_t EBO() const;

  // Return amount of vertices
  uint32_t VerticeCount() const;

  // Return amount of indices
  uint32_t IndiceCount() const;

  // Return mesh material index related to parent model
  uint32_t MaterialIndex() const;

  // Skinned meshes: a draw takes at most max_bones bones, each following a
  // skeleton node
  static constexpr uint32_t max_bones = 64;
  bool Skinned() const { return bgfx::isValid(skin_vbo_); }
  uint32_t BoneCount() const { return static_cast<uint32_t>(bone_nodes_.size()); }
  // The bones' matrices from the `count` nodes' model-space transforms
  void SkinMatrices(const glm::mat4* pose, uint32_t count,
                    glm::mat4* out) const;

  // Sphere around the vertices, in mesh space (for culling)
  const glm::vec3& BoundsCenter() const { return bounds_center_; }
  float BoundsRadius() const { return bounds_radius_; }

  // How far it draws and what draws in its place farther away (its model's
  // DrawSettings): it dithers out from FadeStart to FadeEnd metres from the
  // viewer (FadeEnd <= FadeStart: no limit)
  struct Lod {
    float distance;
    const Mesh* mesh;
  };
  float FadeStart() const { return fade_start_; }
  float FadeEnd() const { return fade_end_; }
  // The coarsest LOD whose distance it is past, or itself
  const Mesh& ForDistance(float distance) const;
  const std::vector<Lod>& Lods() const { return lods_; }
  void SetDraw(float fade_start, float fade_end, std::vector<Lod> lods);

private:
  bgfx::VertexBufferHandle vbo_ = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle ibo_ = BGFX_INVALID_HANDLE;
  // Second stream of a skinned mesh: four bone slots and weights per vertex
  bgfx::VertexBufferHandle skin_vbo_ = BGFX_INVALID_HANDLE;
  std::vector<int> bone_nodes_;
  std::vector<glm::mat4> bone_offsets_;

  uint32_t index_count_{0};  // TODO: remove this

  uint32_t material_index_{0};

  glm::vec3 bounds_center_{0.0f};
  float bounds_radius_{0.0f};
  float fade_start_{0.0f};
  float fade_end_{0.0f};
  std::vector<Lod> lods_;  // nearest first
  void SetBounds(const uint8_t* positions, uint32_t count, uint32_t stride);

  // Vertex layout: VertexData + smoothed normal (TexCoord1) for outlines
  static bgfx::VertexLayout layout_;
  static void EnsureLayout();
};

#endif  // MESH_H
