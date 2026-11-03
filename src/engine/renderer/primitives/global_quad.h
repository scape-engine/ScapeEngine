#ifndef GLOBAL_QUAD_H
#define GLOBAL_QUAD_H

#include <bgfx/bgfx.h>

// Fullscreen geometry for screen-space and render target passes. Drawn as one
// oversized triangle: a_position is the NDC xy of the covered view
namespace GlobalQuad {

void Submit(bgfx::ViewId view_id, bgfx::ProgramHandle program,
            uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);

}  // namespace GlobalQuad

#endif  // GLOBAL_QUAD_H
