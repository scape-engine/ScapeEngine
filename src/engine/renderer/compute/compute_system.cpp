#include "compute_system.h"

#include "engine/context/engine_context.h"
#include "engine/core/view_ids.h"
#include "engine/ecs/components/compute_buffer_component.h"
#include "engine/ecs/components/compute_job_component.h"
#include "engine/ecs/components/render_texture_component.h"
#include "engine/ecs/components/shader_params_component.h"
#include "engine/renderer/graphics_renderer.h"
#include "engine/renderer/shaders/shader_manager.h"

namespace ComputeSystem {
namespace {

GraphicsRenderer& Graphics() {
  return static_cast<GraphicsRenderer&>(EngineContext::Renderer());
}

void ReleaseTextures(Registry& registry, Entity entity) {
  for (auto& texture : registry.get<RenderTextureComponent>(entity).textures_)
    TextureRegistry::Instance().Release(texture.id);
}

void ReleaseBuffers(Registry& registry, Entity entity) {
  for (auto& buffer : registry.get<ComputeBufferComponent>(entity).buffers_)
    ComputeBufferRegistry::Instance().Release(buffer.id);
}

void SetUniforms(ECS& ecs, Entity entity) {
  if (!ecs.Has<ShaderParamsComponent>(entity))
    return;
  for (const auto& uniform : ecs.Get<ShaderParamsComponent>(entity).uniforms_) {
    if (uniform.values.empty())
      continue;
    const auto count = static_cast<uint16_t>(uniform.values.size());
    bgfx::setUniform(
        Graphics().CreateUniform(uniform.name, bgfx::UniformType::Vec4, count),
        uniform.values.data(), count);
  }
}

// Makes missing textures and uploads dirty buffers
void Prepare(ECS& ecs) {
  for (auto [entity, set] : ecs.View<RenderTextureComponent>().each()) {
    for (auto& texture : set.textures_) {
      // TODO: remake a texture whose size or format changed
      if (texture.id == INVALID_TEXTURE)
        texture.id = TextureRegistry::Instance().CreateStorage(
            texture.width, texture.height, texture.layers, texture.format,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    }
  }
  for (auto [entity, set] : ecs.View<ComputeBufferComponent>().each()) {
    for (auto& buffer : set.buffers_) {
      if (buffer.id == INVALID_COMPUTE_BUFFER)
        buffer.id = ComputeBufferRegistry::Instance().Create(
            static_cast<uint32_t>(buffer.data.size()));
      if (buffer.dirty) {
        ComputeBufferRegistry::Instance().Update(buffer.id, buffer.data);
        buffer.dirty = false;
      }
    }
  }
}

}  // namespace

void Connect(Registry& registry) {
  registry.on_destroy<RenderTextureComponent>().connect<&ReleaseTextures>();
  registry.on_destroy<ComputeBufferComponent>().connect<&ReleaseBuffers>();
}

void Dispatch(ECS& ecs) {
  if (!(bgfx::getCaps()->supported & BGFX_CAPS_COMPUTE))
    return;

  Prepare(ecs);

  for (auto [entity, job] : ecs.View<ComputeJobComponent>().each()) {
    if (!job.enabled_)
      continue;
    const auto* buffers = ecs.Has<ComputeBufferComponent>(entity)
                              ? &ecs.Get<ComputeBufferComponent>(entity)
                              : nullptr;
    const auto* textures = ecs.Has<RenderTextureComponent>(entity)
                               ? &ecs.Get<RenderTextureComponent>(entity)
                               : nullptr;

    for (const auto& dispatch : job.dispatches_) {
      const bgfx::ProgramHandle program =
          ShaderManager::LoadComputeProgram(dispatch.shader);
      if (!bgfx::isValid(program))
        continue;

      uint8_t stage = 0;
      bool bound = true;
      for (uint8_t index : dispatch.buffers) {
        bgfx::DynamicVertexBufferHandle handle = BGFX_INVALID_HANDLE;
        if (buffers && index < buffers->buffers_.size())
          handle = ComputeBufferRegistry::Instance().Get(
              buffers->buffers_[index].id);
        if (!(bound = bgfx::isValid(handle)))
          break;
        bgfx::setBuffer(stage++, handle, bgfx::Access::Read);
      }
      for (uint8_t index : dispatch.textures) {
        if (!bound)
          break;
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        bgfx::TextureFormat::Enum format = bgfx::TextureFormat::Unknown;
        if (textures && index < textures->textures_.size()) {
          handle = TextureRegistry::Instance().Get(
              textures->textures_[index].id);
          format = textures->textures_[index].format;
        }
        if (!(bound = bgfx::isValid(handle)))
          break;
        bgfx::setImage(stage++, handle, 0, bgfx::Access::Write, format);
      }
      // A missing resource would leave a stage unbound: skip the dispatch
      if (!bound) {
        bgfx::discard();
        continue;
      }

      SetUniforms(ecs, entity);
      bgfx::dispatch(ViewID::COMPUTE, program, dispatch.groups[0],
                     dispatch.groups[1], dispatch.groups[2]);
    }
  }
}

void BindForDraw(ECS& ecs, Entity entity) {
  SetUniforms(ecs, entity);
  if (!ecs.Has<RenderTextureComponent>(entity))
    return;
  uint8_t stage = GraphicsRenderer::ENTITY_TEXTURE_STAGE;
  for (const auto& texture :
       ecs.Get<RenderTextureComponent>(entity).textures_) {
    bgfx::setTexture(
        stage++,
        Graphics().CreateUniform(texture.name, bgfx::UniformType::Sampler),
        TextureRegistry::Instance().Get(texture.id));
  }
}

}  // namespace ComputeSystem
