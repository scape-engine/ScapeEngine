#include "sky_light.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <memory>
#include <string>

#include "engine/context/engine_context.h"
#include "engine/core/logger.h"
#include "engine/core/view_ids.h"
#include "engine/ecs/components/light_component.h"
#include "engine/renderer/graphics_renderer.h"
#include "engine/renderer/primitives/global_quad.h"
#include "engine/renderer/skybox/skybox.h"
#include "engine/renderer/texture/texture2d.h"

namespace SkyLight {

namespace {

// Capture resolution and prefiltered roughness levels
constexpr uint16_t CAPTURE_SIZE = 128;
constexpr uint16_t PREFILTER_MIPS = 5;
constexpr uint16_t SH_COEFFICIENTS = 9;

static_assert(ViewID::SKY_SH == ViewID::SKY_CAPTURE_BASE + 6,
              "Sky light views overlap");
static_assert(ViewID::SKY_PREFILTER_BASE + 6 * PREFILTER_MIPS <=
                  ViewID::PRE_PASS,
              "Sky light views overlap the ambient occlusion views");

// Render targets
bgfx::TextureHandle capture_cube = BGFX_INVALID_HANDLE;
bgfx::TextureHandle prefilter_cube = BGFX_INVALID_HANDLE;
bgfx::FrameBufferHandle capture_fb[6];
bgfx::FrameBufferHandle prefilter_fb[6 * PREFILTER_MIPS];
bgfx::FrameBufferHandle sh_fb = BGFX_INVALID_HANDLE;

// Programs (owned by the shader manager)
bgfx::ProgramHandle sh_program = BGFX_INVALID_HANDLE;
bgfx::ProgramHandle prefilter_program = BGFX_INVALID_HANDLE;
bgfx::ProgramHandle equirect_program = BGFX_INVALID_HANDLE;

// SourceCubemap image, reloaded when its path changes
std::shared_ptr<Texture> equirect;
std::string equirect_path;

// Captured since the sky light was last enabled
bool captured = false;

GraphicsRenderer& Graphics() {
  return static_cast<GraphicsRenderer&>(EngineContext::Renderer());
}

// Cube faces are sampled with a top left origin
float FlipY() {
  return bgfx::getCaps()->originBottomLeft ? -1.0f : 1.0f;
}

}  // namespace

// Returns true once a valid image is loaded for the path
static bool LoadEquirect(const std::string& path) {
  if (path != equirect_path) {
    equirect_path = path;
    equirect = path.empty() ? nullptr
                            : Texture::Load2D(path, TextureType::HDR);
  }
  return equirect && equirect->Valid();
}

// Sky radiance into the capture cube
static void CaptureScene(Skybox& skybox) {
  // 90 degree square projection, one view per face
  float projection[16];
  bx::mtxProj(projection, 90.0f, 1.0f, 0.1f, 10.0f,
              bgfx::getCaps()->homogeneousDepth, bx::Handedness::Left);
  if (bgfx::getCaps()->originBottomLeft) {
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
}

// Equirect image into the capture cube
static void CaptureCubemap(const SkyLightComponent& light) {
  auto& graphics = Graphics();
  const float source[4] = {bx::toRad(light.source_cubemap_angle_), 0.0f, 0.0f,
                           0.0f};

  for (uint16_t face = 0; face < 6; ++face) {
    const bgfx::ViewId view_id = ViewID::SKY_CAPTURE_BASE + face;
    const float params[4] = {float(face), FlipY(), 0.0f, float(CAPTURE_SIZE)};

    bgfx::setViewRect(view_id, 0, 0, CAPTURE_SIZE, CAPTURE_SIZE);
    bgfx::setViewFrameBuffer(view_id, capture_fb[face]);
    bgfx::setUniform(graphics.GetSkyLightCaptureUniform(), params);
    bgfx::setUniform(graphics.GetSkyLightSourceUniform(), source);

    // Wraps around horizontally, clamped at the poles
    bgfx::setTexture(0, graphics.GetSkyEquirectSamplerUniform(),
                     equirect->Handle(), BGFX_SAMPLER_V_CLAMP);
    GlobalQuad::Submit(view_id, equirect_program);
  }
}

// Diffuse SH and prefiltered reflections from the capture cube
static void Filter(const SkyLightComponent& light) {
  auto& graphics = Graphics();
  const float lower_hemisphere[4] = {
      light.lower_hemisphere_color_.r, light.lower_hemisphere_color_.g,
      light.lower_hemisphere_color_.b,
      light.lower_hemisphere_is_solid_color_ ? 1.0f : 0.0f};

  // Diffuse: project the capture onto SH
  const float sh_params[4] = {0.0f, FlipY(), 0.0f, float(CAPTURE_SIZE)};
  bgfx::setViewRect(ViewID::SKY_SH, 0, 0, SH_COEFFICIENTS, 1);
  bgfx::setViewFrameBuffer(ViewID::SKY_SH, sh_fb);
  bgfx::setUniform(graphics.GetSkyLightCaptureUniform(), sh_params);
  bgfx::setUniform(graphics.GetLowerHemisphereUniform(), lower_hemisphere);
  bgfx::setTexture(0, graphics.GetSkyCaptureSamplerUniform(), capture_cube);
  GlobalQuad::Submit(ViewID::SKY_SH, sh_program);

  // Specular: one roughness level per mip, from mirror to fully rough
  for (uint16_t mip = 0; mip < PREFILTER_MIPS; ++mip) {
    const uint16_t size = CAPTURE_SIZE >> mip;
    const float roughness = float(mip) / float(PREFILTER_MIPS - 1);

    for (uint16_t face = 0; face < 6; ++face) {
      const uint16_t index = mip * 6 + face;
      const bgfx::ViewId view_id = ViewID::SKY_PREFILTER_BASE + index;
      const float params[4] = {float(face), FlipY(), roughness,
                               float(CAPTURE_SIZE)};

      bgfx::setViewRect(view_id, 0, 0, size, size);
      bgfx::setViewFrameBuffer(view_id, prefilter_fb[index]);
      bgfx::setUniform(graphics.GetSkyLightCaptureUniform(), params);
      bgfx::setUniform(graphics.GetLowerHemisphereUniform(), lower_hemisphere);
      bgfx::setTexture(0, graphics.GetSkyCaptureSamplerUniform(),
                       capture_cube);
      GlobalQuad::Submit(view_id, prefilter_program);
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
  equirect_program = EngineContext::Shader().LoadProgram(
      "sky_light_equirect", "vs_sky_light.bin", "fs_sky_light_equirect.bin");

  if (!bgfx::isValid(sh_program) || !bgfx::isValid(prefilter_program) ||
      !bgfx::isValid(equirect_program)) {
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

  equirect.reset();
  equirect_path.clear();
}

void Update(Skybox& skybox, SkyLightComponent& light) {
  const bool source_cubemap =
      light.source_type_ == SkyLightSource::SourceCubemap;

  // Nothing to light with until a valid image is assigned
  if (source_cubemap && !LoadEquirect(light.cubemap_path_)) {
    Disable();
    return;
  }

  // Real-time capture only applies to the scene, an image never changes
  const bool real_time = light.real_time_capture_ && !source_cubemap;
  if (real_time || !captured || light.recapture_) {
    if (source_cubemap) {
      CaptureCubemap(light);
    } else {
      CaptureScene(skybox);
    }
    Filter(light);

    captured = true;
    light.recapture_ = false;
  }

  auto& graphics = Graphics();
  const float sky_light[4] = {light.color_.r * light.intensity_,
                              light.color_.g * light.intensity_,
                              light.color_.b * light.intensity_, 1.0f};
  const float params[4] = {float(PREFILTER_MIPS - 1), 0.0f, 0.0f, 0.0f};
  bgfx::setUniform(graphics.GetSkyLightUniform(), sky_light);
  bgfx::setUniform(graphics.GetSkyLightParamsUniform(), params);
}

void Disable() {
  // Capture again once a sky light is back
  captured = false;

  const float none[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  bgfx::setUniform(Graphics().GetSkyLightUniform(), none);
}

void BindTextures() {
  auto& graphics = Graphics();
  bgfx::setTexture(GraphicsRenderer::SKY_SH_STAGE,
                   graphics.GetSkySHSamplerUniform(), bgfx::getTexture(sh_fb));
  bgfx::setTexture(GraphicsRenderer::SKY_PREFILTER_STAGE,
                   graphics.GetSkyPrefilterSamplerUniform(), prefilter_cube);
}

}  // namespace SkyLight
