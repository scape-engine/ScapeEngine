#ifndef COMPUTE_SYSTEM_H
#define COMPUTE_SYSTEM_H

#include <bgfx/bgfx.h>

#include "engine/ecs/world.h"

// Runs the compute jobs of the scene (ComputeJobComponent) on the GPU and
// keeps the GPU side of the components they use: makes missing
// RenderTextureComponent textures, uploads dirty ComputeBufferComponent data
// and releases both when their component goes. Knows nothing of what the
// jobs compute; the systems that fill the components do
namespace ComputeSystem {

// Releases the GPU resources of RenderTexture and ComputeBuffer components
// when they are removed or their entity destroyed
void Connect(Registry& registry);

// Every enabled job, in the COMPUTE view. Once per frame, before the views
// that sample what they write. Does nothing without compute support
void Dispatch(ECS& ecs);

// Sets the entity's ShaderParamsComponent uniforms and binds its
// RenderTextureComponent textures (from ENTITY_TEXTURE_STAGE) for its next
// draw
void BindForDraw(ECS& ecs, Entity entity);

}  // namespace ComputeSystem

#endif  // COMPUTE_SYSTEM_H
