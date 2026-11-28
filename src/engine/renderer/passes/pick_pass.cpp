#include "pick_pass.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/type_ptr.hpp>

#include "engine/context/engine_context.h"
#include "engine/core/logger.h"
#include "engine/core/view_ids.h"
#include "engine/ecs/components/instanced_mesh_component.h"
#include "engine/renderer/draw_list.h"
#include "engine/renderer/model/mesh.h"

void PickPass::Create() {
  const bgfx::Caps* caps = bgfx::getCaps();
  if (!(caps->supported & BGFX_CAPS_TEXTURE_READ_BACK) ||
      !(caps->supported & BGFX_CAPS_TEXTURE_BLIT)) {
    Logger::getInstance().Log(LogLevel::Warning,
                              "PickPass: no texture read-back, no picking");
    return;
  }
  // The pre-pass's vertex shaders: the same positions, fades and skinning
  program_ = EngineContext::Shader().LoadProgram("pick", "vs_pre_pass.bin",
                                                 "fs_pick.bin");
  skinned_program_ = EngineContext::Shader().LoadProgram(
      "pick", "vs_pre_pass_skinned.bin", "fs_pick.bin");
  u_pick_id_ = bgfx::createUniform("u_pickId", bgfx::UniformType::Vec4);

  colour_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                  BGFX_TEXTURE_RT | BGFX_SAMPLER_POINT);
  surface_ = bgfx::createTexture2D(1, 1, false, 1,
                                   bgfx::TextureFormat::RGBA32F,
                                   BGFX_TEXTURE_RT | BGFX_SAMPLER_POINT);
  const bgfx::TextureHandle attachments[3] = {
      colour_, surface_,
      bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::D32F,
                            BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY)};
  target_ = bgfx::createFrameBuffer(3, attachments, true);
  read_ = bgfx::createTexture2D(
      1, 1, false, 1, bgfx::TextureFormat::RGBA8,
      BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK | BGFX_SAMPLER_POINT);
  surface_read_ = bgfx::createTexture2D(
      1, 1, false, 1, bgfx::TextureFormat::RGBA32F,
      BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK | BGFX_SAMPLER_POINT);
}

void PickPass::Destroy() {
  if (bgfx::isValid(target_))
    bgfx::destroy(target_);
  if (bgfx::isValid(read_))
    bgfx::destroy(read_);
  if (bgfx::isValid(surface_read_))
    bgfx::destroy(surface_read_);
  if (bgfx::isValid(u_pick_id_))
    bgfx::destroy(u_pick_id_);
  target_ = BGFX_INVALID_HANDLE;
  colour_ = read_ = surface_ = surface_read_ = BGFX_INVALID_HANDLE;
  u_pick_id_ = BGFX_INVALID_HANDLE;
}

void PickPass::Request(const glm::vec2& pixel, uint16_t width, uint16_t height,
                       const glm::mat4& view, const glm::mat4& projection,
                       const glm::vec3& viewer) {
  if (!bgfx::isValid(program_) || !bgfx::isValid(target_) || width == 0 ||
      height == 0)
    return;

  // A projection whose whole view is the pixel: the draw list keeps only what
  // can cover it
  const glm::vec2 ndc(2.0f * (pixel.x + 0.5f) / width - 1.0f,
                      1.0f - 2.0f * (pixel.y + 0.5f) / height);
  glm::mat4 zoom(1.0f);
  zoom[0][0] = width;
  zoom[1][1] = height;
  zoom[3][0] = -ndc.x * width;
  zoom[3][1] = -ndc.y * height;
  const glm::mat4 pick_projection = zoom * projection;
  // The pixel's ray in view space, through two depths of the projection
  const glm::mat4 inverse_projection = glm::inverse(projection);
  const auto unproject = [&](float z) {
    const glm::vec4 p = inverse_projection * glm::vec4(ndc, z, 1.0f);
    return glm::vec3(p) / p.w;
  };
  ray_from_ = unproject(0.25f);
  ray_ = unproject(0.75f) - ray_from_;
  inverse_view_ = glm::inverse(view);

  DrawList draws;
  DrawList::Build({{&draws, pick_projection * view}}, viewer, {}, false);

  bgfx::setViewRect(ViewID::PICK, 0, 0, 1, 1);
  bgfx::setViewFrameBuffer(ViewID::PICK, target_);
  bgfx::setViewClear(ViewID::PICK, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                     0x00000000, 0.0f, 0);
  bgfx::setViewTransform(ViewID::PICK, glm::value_ptr(view),
                         glm::value_ptr(pick_projection));
  bgfx::touch(ViewID::PICK);

  // Without instancing every draw is a single: its entity, and an instance
  // by its matrix's place among the component's
  drawn_.clear();
  const uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                         BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_GREATER;
  auto& ecs = ECS::Main();
  for (const DrawList::Single& single : draws.singles) {
    Hit hit{single.entity, -1};
    if (const auto* set =
            ecs.Reg().try_get<InstancedMeshComponent>(single.entity);
        set && !set->worlds_.empty() && single.model >= set->worlds_.data() &&
        single.model < set->worlds_.data() + set->worlds_.size())
      hit.instance = static_cast<int>(single.model - set->worlds_.data());
    drawn_.push_back(hit);
    const uint32_t id = static_cast<uint32_t>(drawn_.size());
    const glm::vec4 colour(id & 0xff, (id >> 8) & 0xff, (id >> 16) & 0xff,
                           (id >> 24) & 0xff);
    bgfx::setUniform(u_pick_id_, glm::value_ptr(colour / 255.0f));
    bgfx::setTransform(glm::value_ptr(*single.model));
    DrawList::SetFade(single.fade);
    single.mesh->Bind();
    bgfx::setState(state | DrawList::Cull(single.material,
                                          DrawList::Mirrored(*single.model)));
    const bool skinned =
        bgfx::isValid(skinned_program_) &&
        DrawList::SetBones(*single.mesh, single.pose, single.pose_count);
    bgfx::submit(ViewID::PICK, skinned ? skinned_program_ : program_);
  }

  bgfx::touch(ViewID::PICK_READ);
  bgfx::blit(ViewID::PICK_READ, read_, 0, 0, colour_);
  bgfx::blit(ViewID::PICK_READ, surface_read_, 0, 0, surface_);
  ready_frame_ = std::max(bgfx::readTexture(read_, pixel_),
                          bgfx::readTexture(surface_read_, surface_pixel_));
  pending_ = true;
}

std::optional<PickPass::Hit> PickPass::Take() {
  if (!pending_ || EngineContext::Frame() < ready_frame_)
    return std::nullopt;
  pending_ = false;
  const uint32_t id = pixel_[0] | (pixel_[1] << 8) | (pixel_[2] << 16) |
                      (static_cast<uint32_t>(pixel_[3]) << 24);
  if (id == 0 || id > drawn_.size())
    return Hit{};
  Hit hit = drawn_[id - 1];
  // Along the pixel's ray to the surface's view depth
  const float depth = surface_pixel_[3];
  const glm::vec3 point =
      ray_from_ +
      ray_ * (std::abs(ray_.z) > 0.0f ? (depth - ray_from_.z) / ray_.z : 0.0f);
  hit.position = inverse_view_ * glm::vec4(point, 1.0f);
  const glm::vec3 normal = glm::mat3(inverse_view_) *
                           glm::vec3(surface_pixel_[0], surface_pixel_[1],
                                     surface_pixel_[2]);
  if (glm::dot(normal, normal) > 0.0f)
    hit.normal = glm::normalize(normal);
  return hit;
}
