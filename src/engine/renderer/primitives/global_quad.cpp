#include "global_quad.h"

#include <cstring>

namespace GlobalQuad {

void Submit(bgfx::ViewId view_id, bgfx::ProgramHandle program,
            uint64_t state) {
  static bgfx::VertexLayout layout;
  if (!layout.m_stride) {
    layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .end();
  }

  if (bgfx::getAvailTransientVertexBuffer(3, layout) < 3)
    return;

  bgfx::TransientVertexBuffer tvb;
  bgfx::allocTransientVertexBuffer(&tvb, 3, layout);

  const float triangle[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  memcpy(tvb.data, triangle, sizeof(triangle));

  bgfx::setVertexBuffer(0, &tvb);
  bgfx::setState(state);
  bgfx::submit(view_id, program);
}

}  // namespace GlobalQuad
