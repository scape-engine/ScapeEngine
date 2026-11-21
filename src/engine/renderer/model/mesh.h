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

  // Sphere around the vertices, in mesh space (for culling)
  const glm::vec3& BoundsCenter() const { return bounds_center_; }
  float BoundsRadius() const { return bounds_radius_; }

private:
  bgfx::VertexBufferHandle vbo_ = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle ibo_ = BGFX_INVALID_HANDLE;

  uint32_t index_count_{0};  // TODO: remove this

  uint32_t material_index_{0};

  glm::vec3 bounds_center_{0.0f};
  float bounds_radius_{0.0f};
  void SetBounds(const uint8_t* positions, uint32_t count, uint32_t stride);

  // Vertex layout: VertexData + smoothed normal (TexCoord1) for outlines
  static bgfx::VertexLayout layout_;
  static void EnsureLayout();
};

#endif  // MESH_H
