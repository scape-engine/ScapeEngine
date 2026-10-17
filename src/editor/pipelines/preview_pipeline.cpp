#include "preview_pipeline.h"

#include <cstdint>
#include <glm/gtc/type_ptr.hpp>

#include "editor/runtime/runtime.h"
#include "engine/core/logger.h"
#include "engine/core/view_ids.h"
#include "engine/math/transformation.h"
#include "engine/memory/resource_manager.h"
#include "engine/renderer/lighting/light.h"
#include "engine/renderer/model/model.h"

// Static counter to guarantee unique View IDs across all pipeline instances
static uint16_t s_global_preview_view_offset = 0;

PreviewPipeline::PreviewPipeline()
    : preview_program_(BGFX_INVALID_HANDLE),
      outputs_(),
      render_instructions_() {}

void PreviewPipeline::Create() {
  // Load shader
  preview_program_ =
      Runtime::Shader()->LoadProgram("preview", "vs_gltf.bin", "fs_gltf.bin");

  // ! REMOVE THIS
  u_mesh_color_ = bgfx::createUniform("u_meshColor", bgfx::UniformType::Vec4);

  if (!bgfx::isValid(preview_program_)) {
    Logger::getInstance().Log(LogLevel::Error,
                              "PreviewPipeline: Failed to load preview shader");
  }
}

void PreviewPipeline::Destroy() {
  // Destroy all outputs
  for (auto& output : outputs_) {
    if (bgfx::isValid(output.framebuffer))
      bgfx::destroy(output.framebuffer);
    if (bgfx::isValid(output.color_texture))
      bgfx::destroy(output.color_texture);
    if (bgfx::isValid(output.depth_texture))
      bgfx::destroy(output.depth_texture);
  }
  outputs_.clear();

  if (bgfx::isValid(u_mesh_color_))
    bgfx::destroy(u_mesh_color_);

  Logger::getInstance().Log(LogLevel::Debug, "PreviewPipeline: Destroyed");
}

void PreviewPipeline::Render() {
  if (!bgfx::isValid(preview_program_))
    return;

  // Group instructions by output
  std::unordered_map<size_t, std::vector<PreviewRenderInstruction*>> grouped;
  for (auto& inst : render_instructions_) {
    if (inst.output_index < outputs_.size())
      grouped[inst.output_index].push_back(&inst);
  }

  // Render each grouped output
  for (auto& [output_index, instructions] : grouped) {
    PreviewOutput& output = outputs_[output_index];

    // Recreate framebuffer if resize is pending
    if (output.resize_pending) {
      if (output.width > 0 && output.height > 0) {
        // Destroy old FBO and textures
        if (bgfx::isValid(output.framebuffer))
          bgfx::destroy(output.framebuffer);
        if (bgfx::isValid(output.color_texture))
          bgfx::destroy(output.color_texture);
        if (bgfx::isValid(output.depth_texture))
          bgfx::destroy(output.depth_texture);

        // Recreate
        output.color_texture = bgfx::createTexture2D(
            output.width, output.height, false, 1, bgfx::TextureFormat::BGRA8,
            BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

        output.depth_texture = bgfx::createTexture2D(
            output.width, output.height, false, 1, bgfx::TextureFormat::D24S8,
            BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY);

        const bgfx::TextureHandle attachments[2] = {output.color_texture,
                                                    output.depth_texture};
        output.framebuffer = bgfx::createFrameBuffer(BX_COUNTOF(attachments),
                                                     attachments, false);
      }
      output.resize_pending = false;
    }

    if (!bgfx::isValid(output.framebuffer))
      continue;

    // Configure preview view
    bgfx::ViewId view_id = output.view_id;
    bgfx::setViewRect(view_id, 0, 0, output.width, output.height);
    bgfx::setViewFrameBuffer(view_id, output.framebuffer);

    // Process instructions for this output
    for (auto* instruction : instructions) {
      if (instruction->clear_output) {
        uint32_t clear_color =
            (uint32_t(instruction->background_color.r * 255) << 24) |
            (uint32_t(instruction->background_color.g * 255) << 16) |
            (uint32_t(instruction->background_color.b * 255) << 8) |
            (uint32_t(instruction->background_color.a * 255));
        bgfx::setViewClear(view_id, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                           clear_color, 1.0f, 0);
        bgfx::touch(view_id);
      }

      if (!instruction->model)
        continue;

      // ------ Build view and projection matrices ------
      // Build model matrix
      glm::mat4 model_mtx =
          Transformation::Model(instruction->model_transform.position_,
                                instruction->model_transform.rotation_,
                                instruction->model_transform.scale_);

      glm::mat4 view_mtx =
          Transformation::View(instruction->camera_transform.position_,
                               instruction->camera_transform.rotation_);

      float aspect = (output.height > 0)
                         ? float(output.width) / float(output.height)
                         : 1.0f;

      // Explicit radians bypasses Transformation::Projection FOV bugs
      // Near plane set to 0.01f so the camera doesn't slice the model when
      // zoomed

      glm::mat4 proj_mtx =
          Transformation::Projection(45.0f, aspect, 0.01f, 1000.0f);

      bgfx::setViewTransform(view_id, glm::value_ptr(view_mtx),
                             glm::value_ptr(proj_mtx));

      uint32_t mesh_count = instruction->model->NLoadedMeshes();
      for (uint32_t m = 0; m < mesh_count; m++) {
        if (!instruction->mesh_filter.empty() &&
            std::find(instruction->mesh_filter.begin(),
                      instruction->mesh_filter.end(),
                      m) == instruction->mesh_filter.end()) {
          continue;
        }

        const Mesh* mesh = instruction->model->QueryMesh(m);
        if (!mesh)
          continue;

        // Submit per-mesh transform override or default model transform
        auto mt = instruction->mesh_transforms.find(m);
        if (mt != instruction->mesh_transforms.end()) {

          glm::mat4 mtx = Transformation::Model(
              mt->second.position_, mt->second.rotation_, mt->second.scale_);
          bgfx::setTransform(glm::value_ptr(mtx));
        } else {
          bgfx::setTransform(glm::value_ptr(model_mtx));
        }

        // ! REMOVE THIS - only used for procmodel (applies diffuse light to
        // coloring)
        auto mc = instruction->mesh_colors.find(m);
        glm::vec4 color = (mc != instruction->mesh_colors.end())
                              ? glm::vec4(mc->second, 1.0f)
                              : glm::vec4(1.0f);
        bgfx::setUniform(u_mesh_color_,
                         glm::value_ptr(color));  // Matte Lighting

        // ----------------------------
        // 1. Set ambient light (Brightened to 0.4 so dark materials are
        // visible)
        Light::SetAmbient(glm::vec3(0.4f));

        // 2. Set primary directional light (Bright white sunlight to pop the
        // model details)
        Light::SetDirectionalLight(
            0, glm::normalize(glm::vec3(-0.5f, -1.0f, -0.5f)),  // Direction
            glm::vec3(0.2f),  // Ambient intensity
            glm::vec3(3.0f),  // Diffuse intensity
            glm::vec3(1.0f)   // Specular intensity
        );

        // 3. using exactly 1 directional light, 0 point, 0 spot
        Light::SetLightCounts(1, 0, 0);

        // 4. Push data to BGFX uniforms for this render pass
        Light::ApplyUniforms();

        mesh->Bind();

        auto* graphics = static_cast<GraphicsRenderer*>(Runtime::Renderer());
        bgfx::setTexture(0, graphics->GetAlbedoSamplerUniform(),
                         graphics->GetWhiteTexture());

        bgfx::setState(BGFX_STATE_DEFAULT | BGFX_STATE_CULL_CW);
        bgfx::submit(view_id, preview_program_);
      }
    }
  }

  render_instructions_.clear();
}

size_t PreviewPipeline::CreateOutput() {
  // Out of view IDs: fail instead of overlapping other passes' views.
  // Every PreviewPipeline accessor bounds-checks the returned index.
  if (ViewID::PREVIEW_PASS_BASE + s_global_preview_view_offset >
      ViewID::PREVIEW_PASS_MAX) {
    Logger::getInstance().Log(LogLevel::Error,
                              "PreviewPipeline: Exceeded maximum View IDs!");
    return SIZE_MAX;
  }

  PreviewOutput output;
  output.width = 250;
  output.height = 250;

  // Create color and depth textures
  output.color_texture = bgfx::createTexture2D(
      output.width, output.height, false, 1, bgfx::TextureFormat::BGRA8,
      BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

  output.depth_texture = bgfx::createTexture2D(
      output.width, output.height, false, 1, bgfx::TextureFormat::D24S8,
      BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY);

  // Create framebuffer from attachments
  const bgfx::TextureHandle attachments[2] = {output.color_texture,
                                              output.depth_texture};
  output.framebuffer =
      bgfx::createFrameBuffer(BX_COUNTOF(attachments), attachments, false);

  if (!bgfx::isValid(output.framebuffer)) {
    Logger::getInstance().Log(LogLevel::Error,
                              "PreviewPipeline: Failed to create output FBO");
  }

  output.view_id = ViewID::PREVIEW_PASS_BASE + s_global_preview_view_offset++;

  outputs_.push_back(output);
  return outputs_.size() - 1;
}

const PreviewOutput& PreviewPipeline::GetOutput(size_t index) const {
  static const PreviewOutput kEmpty{};

  if (index >= outputs_.size())
    return kEmpty;

  return outputs_[index];
}

bgfx::TextureHandle PreviewPipeline::GetOutputTexture(size_t index) const {
  if (index >= outputs_.size())
    return BGFX_INVALID_HANDLE;

  return outputs_[index].color_texture;
}

void PreviewPipeline::ResizeOutput(size_t index, float width, float height) {
  if (index >= outputs_.size())
    return;

  PreviewOutput& output = outputs_[index];
  uint16_t w = static_cast<uint16_t>(width);
  uint16_t h = static_cast<uint16_t>(height);

  if (w == output.width && h == output.height)
    return;

  output.width = w;
  output.height = h;
  output.resize_pending = true;
}

void PreviewPipeline::AddRenderInstruction(
    const PreviewRenderInstruction instruction) {
  render_instructions_.push_back(instruction);
}
