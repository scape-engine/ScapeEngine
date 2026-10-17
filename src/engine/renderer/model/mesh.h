#ifndef MESH_H
#define MESH_H

#include <bgfx/bgfx.h>
#include <vector>

#include "engine/core/types/geometry_data.h"

class Mesh {
public:
  explicit Mesh(const Geometry::MeshData& src);
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

private:
  bgfx::VertexBufferHandle vbo_ = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle ibo_ = BGFX_INVALID_HANDLE;

  uint32_t index_count_{0};  // TODO: remove this

  uint32_t material_index_{0};

  // Vertex layout: VertexData + smoothed normal (TexCoord1) for outlines
  static bgfx::VertexLayout layout_;
  static void EnsureLayout();
};

#endif  // MESH_H
