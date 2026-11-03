#include "sky_light.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <cstring>

#include "engine/context/engine_context.h"
#include "engine/core/logger.h"
#include "engine/core/view_ids.h"
#include "engine/ecs/components/light_component.h"
#include "engine/renderer/skybox/skybox.h"

namespace SkyLight {

namespace {

// Capture resolution and prefiltered roughness levels
constexpr uint16_t CAPTURE_SIZE = 128;
constexpr uint16_t PREFILTER_MIPS = 5;
constexpr uint16_t SH_COEFFICIENTS = 9;

static_assert(ViewID::SKY_SH == ViewID::SKY_CAPTURE_BASE + 6,
              "Sky light views overlap");
static_assert(ViewID::SKY_PREFILTER_BASE + 6 * PREFILTER_MIPS <=
                  ViewID::PREVIEW_PASS_BASE,
              "Sky light views overlap the preview views");

// Render targets
bgfx::TextureHandle capture_cube = BGFX_INVALID_HANDLE;
bgfx::TextureHandle prefilter_cube = BGFX_INVALID_HANDLE;
bgfx::FrameBufferHandle capture_fb[6];
bgfx::FrameBufferHandle prefilter_fb[6 * PREFILTER_MIPS];
bgfx::FrameBufferHandle sh_fb = BGFX_INVALID_HANDLE;

// Programs (owned by the shader manager)
bgfx::ProgramHandle sh_program = BGFX_INVALID_HANDLE;
bgfx::ProgramHandle prefilter_program = BGFX_INVALID_HANDLE;

// Uniform handles
bgfx::UniformHandle s_skyCapture = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_skySH = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_skyPrefilter = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_skyLight = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_skyLightParams = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_skyLightCapture = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_lowerHemisphere = BGFX_INVALID_HANDLE;

bgfx::VertexLayout screen_layout;

// Captured since the sky light was last enabled
bool captured = false;

}  // namespace

// Oversized triangle covering the whole view
static void SubmitFullscreen(bgfx::ViewId view_id,
                             bgfx::ProgramHandle program) {
  if (bgfx::getAvailTransientVertexBuffer(3, screen_layout) < 3)
    return;

  bgfx::TransientVertexBuffer tvb;
  bgfx::allocTransientVertexBuffer(&tvb, 3, screen_layout);

  const float triangle[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  memcpy(tvb.data, triangle, sizeof(triangle));

  bgfx::setVertexBuffer(0, &tvb);
  bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
  bgfx::submit(view_id, program);
}

static void Capture(Skybox& skybox, const SkyLightComponent& light) {
  // Cube faces are sampled with a top left origin
  const bool origin_bottom_left = bgfx::getCaps()->originBottomLeft;
  const float flip_y = origin_bottom_left ? -1.0f : 1.0f;

  // 90 degree square projection, one view per face
  float projection[16];
  bx::mtxProj(projection, 90.0f, 1.0f, 0.1f, 10.0f,
              bgfx::getCaps()->homogeneousDepth, bx::Handedness::Left);
  if (origin_bottom_left) {
    projection[5] = -projection[5];
  }

  // Face order and orientation of a cubemap (+X, -X, +Y, -Y, +Z, -Z)
  const bx::Vec3 forward[6] = {
      bx::Vec3(1.0f, 0.0f, 0.0f), bx::Vec3(-1.0f, 0.0f, 0.0f),
      bx::Vec3(0.0f, 1.0f, 0.0f), bx::Vec3(0.0f, -1.0f, 0.0f),
      bx::Vec3(0.0f, 0.0f, 1.0f), bx::Vec3(0.0f, 0.0f, -1.0f)};
  const bx::Vec3 up[6] = {
      bx::Vec3(0.0f, 1.0f, 0.0f),  bx::Vec3(0.0f, 1.0f, 0.0f),
      bx::Vec3(0.0f, 0.0f, -1.0f), bx::Vec3(0.0f, 0.0f, 1.0f),
      bx::Vec3(0.0f, 1.0f, 0.0f),  bx::Vec3(0.0f, 1.0f, 0.0f)};

  // Sky radiance into the capture cube, mips generated after each face
  for (uint16_t face = 0; face < 6; ++face) {
    const bgfx::ViewId view_id = ViewID::SKY_CAPTURE_BASE + face;

    float view[16];
    bx::mtxLookAt(view, bx::Vec3(0.0f, 0.0f, 0.0f), forward[face], up[face],
                  bx::Handedness::Left);

    bgfx::setViewRect(view_id, 0, 0, CAPTURE_SIZE, CAPTURE_SIZE);
    bgfx::setViewFrameBuffer(view_id, capture_fb[face]);
    skybox.Submit(view_id, view, projection, /*useInverseViewProj=*/true,
                  /*capture=*/true);
  }

  const float lower_hemisphere[4] = {
      light.lower_hemisphere_color_.r, light.lower_hemisphere_color_.g,
      light.lower_hemisphere_color_.b,
      light.lower_hemisphere_is_solid_color_ ? 1.0f : 0.0f};

  // Diffuse: project the capture onto SH
  const float sh_params[4] = {0.0f, flip_y, 0.0f, float(CAPTURE_SIZE)};
  bgfx::setViewRect(ViewID::SKY_SH, 0, 0, SH_COEFFICIENTS, 1);
  bgfx::setViewFrameBuffer(ViewID::SKY_SH, sh_fb);
  bgfx::setUniform(u_skyLightCapture, sh_params);
  bgfx::setUniform(u_lowerHemisphere, lower_hemisphere);
  bgfx::setTexture(0, s_skyCapture, capture_cube);
  SubmitFullscreen(ViewID::SKY_SH, sh_program);

  // Specular: one roughness level per mip, from mirror to fully rough
  for (uint16_t mip = 0; mip < PREFILTER_MIPS; ++mip) {
    const uint16_t size = CAPTURE_SIZE >> mip;
    const float roughness = float(mip) / float(PREFILTER_MIPS - 1);

    for (uint16_t face = 0; face < 6; ++face) {
      const uint16_t index = mip * 6 + face;
      const bgfx::ViewId view_id = ViewID::SKY_PREFILTER_BASE + index;
      const float params[4] = {float(face), flip_y, roughness,
                               float(CAPTURE_SIZE)};

      bgfx::setViewRect(view_id, 0, 0, size, size);
      bgfx::setViewFrameBuffer(view_id, prefilter_fb[index]);
      bgfx::setUniform(u_skyLightCapture, params);
      bgfx::setUniform(u_lowerHemisphere, lower_hemisphere);
      bgfx::setTexture(0, s_skyCapture, capture_cube);
      SubmitFullscreen(view_id, prefilter_program);
    }
  }
}

void Create() {
  const uint64_t cube_flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_UVW_CLAMP;
  capture_cube = bgfx::createTextureCube(CAPTURE_SIZE, true, 1,
                                         bgfx::TextureFormat::RGBA16F,
                                         cube_flags);
  prefilter_cube = bgfx::createTextureCube(CAPTURE_SIZE, true, 1,
                                           bgfx::TextureFormat::RGBA16F,
                                           cube_flags);

  for (uint16_t face = 0; face < 6; ++face) {
    // Capture mips are generated for filtered sampling
    bgfx::Attachment capture;
    capture.init(capture_cube, bgfx::Access::Write, face, 1, 0,
                 BGFX_RESOLVE_AUTO_GEN_MIPS);
    capture_fb[face] = bgfx::createFrameBuffer(1, &capture);

    // Prefiltered mips are rendered, never generated
    for (uint16_t mip = 0; mip < PREFILTER_MIPS; ++mip) {
      bgfx::Attachment prefilter;
      prefilter.init(prefilter_cube, bgfx::Access::Write, face, 1, mip,
                     BGFX_RESOLVE_NONE);
      prefilter_fb[mip * 6 + face] = bgfx::createFrameBuffer(1, &prefilter);
    }
  }

  // One texel per SH coefficient
  sh_fb = bgfx::createFrameBuffer(
      SH_COEFFICIENTS, 1, bgfx::TextureFormat::RGBA16F,
      BGFX_TEXTURE_RT | BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);

  sh_program = EngineContext::Shader().LoadProgram(
      "sky_light_sh", "vs_sky_light.bin", "fs_sky_light_sh.bin");
  prefilter_program = EngineContext::Shader().LoadProgram(
      "sky_light_prefilter", "vs_sky_light.bin", "fs_sky_light_prefilter.bin");

  s_skyCapture = bgfx::createUniform("s_skyCapture", bgfx::UniformType::Sampler);
  s_skySH = bgfx::createUniform("s_skySH", bgfx::UniformType::Sampler);
  s_skyPrefilter =
      bgfx::createUniform("s_skyPrefilter", bgfx::UniformType::Sampler);
  u_skyLight = bgfx::createUniform("u_skyLight", bgfx::UniformType::Vec4);
  u_skyLightParams =
      bgfx::createUniform("u_skyLightParams", bgfx::UniformType::Vec4);
  u_skyLightCapture =
      bgfx::createUniform("u_skyLightCapture", bgfx::UniformType::Vec4);
  u_lowerHemisphere =
      bgfx::createUniform("u_lowerHemisphere", bgfx::UniformType::Vec4);

  screen_layout.begin()
      .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
      .end();

  if (!bgfx::isValid(sh_program) || !bgfx::isValid(prefilter_program)) {
    Logger::getInstance().Log(LogLevel::Error,
                              "SkyLight: Failed to load capture shaders");
  }

  Logger::getInstance().Log(LogLevel::Info, "SkyLight: Created");
}

void Destroy() {
  auto destroy = [](auto& h) {
    if (bgfx::isValid(h)) {
      bgfx::destroy(h);
      h = BGFX_INVALID_HANDLE;
    }
  };

  for (auto& fb : capture_fb)
    destroy(fb);
  for (auto& fb : prefilter_fb)
    destroy(fb);
  destroy(sh_fb);  // also destroys its texture
  destroy(capture_cube);
  destroy(prefilter_cube);

  destroy(s_skyCapture);
  destroy(s_skySH);
  destroy(s_skyPrefilter);
  destroy(u_skyLight);
  destroy(u_skyLightParams);
  destroy(u_skyLightCapture);
  destroy(u_lowerHemisphere);
}

void Update(Skybox& skybox, SkyLightComponent& light) {
  if (light.real_time_capture_ || !captured || light.recapture_) {
    Capture(skybox, light);
    captured = true;
    light.recapture_ = false;
  }

  const float sky_light[4] = {light.color_.r * light.intensity_,
                              light.color_.g * light.intensity_,
                              light.color_.b * light.intensity_, 1.0f};
  const float params[4] = {float(PREFILTER_MIPS - 1), 0.0f, 0.0f, 0.0f};
  bgfx::setUniform(u_skyLight, sky_light);
  bgfx::setUniform(u_skyLightParams, params);
}

void Disable() {
  // Capture again once a sky light is back
  captured = false;

  const float none[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  bgfx::setUniform(u_skyLight, none);
}

void BindTextures() {
  bgfx::setTexture(1, s_skySH, bgfx::getTexture(sh_fb));
  bgfx::setTexture(2, s_skyPrefilter, prefilter_cube);
}

}  // namespace SkyLight
