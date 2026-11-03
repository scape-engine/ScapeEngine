#ifndef SKYBOX_H
#define SKYBOX_H

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include "engine/memory/resource_manager.h"
#include "engine/renderer/shaders/shader_manager.h"

// Parameters you may want to read from other passes (terrain/water).
struct SkyboxParams {
  float _parametersArray[4];  // { Sun Angular Radius, Procedural Bloom,
                              // Exposure, Time }
  float _sunColorArray[4];    // sun RGB + intensity (w)
  float _skyAmbientArray[4];  // ambient RGB (+ unused w)
  float sunDirShader[4];      // sun direction (xyz, 0)
  float totalBetaR[4];        // Rayleigh totals (xyz, 0)
  float totalBetaM[4];        // Mie totals (xyz, 0)
  float scatterParams[4];     // { g, 0,0,0 }
  float _cycleTime = 0.0f;
};

class Skybox {
public:
  Skybox() = default;

  void Create(ShaderManager* ShaderManager);
  void Destroy();
  void Update(float dt);  // Advance time + sun/ambient

  // Tweak skybox params
  void SetParams(float sunAngularRadius, float proceduralBloom, float exposure,
      float mieAnisotropy = 0.76f);  // TODO: expose mieAnisotropy to UI?

  // Recompute sun direction, luminance, ambient, and scattering
  void ComputeSun();

  // Submit a draw for the skybox to the given view.
  // If useInverseViewProj==true: we compute inverse(view/proj) and set
  // u_viewInv/u_projInv accordingly (this matches your main skybox path). If
  // false: we assume the caller passed a "rotation-only" view (e.g.,
  // reflection) and set uniforms directly
  // If capture==true: linear HDR output without the sun disk or depth test,
  // for sky light cubemap faces
  void Submit(bgfx::ViewId viewId, const float* viewMatrix,
              const float* projMatrix, bool useInverseViewProj,
              bool capture = false);

  // Give access to the computed parameters (for terrain/water)
  const SkyboxParams& GetParams() const { return params_; }

  // Rebuild quad only
  void CreateFullscreenQuad();

  // Day/night cycle control
  void SetCycleSpeed(float speed) { cycle_speed_ = speed; }
  float GetCycleSpeed() const { return cycle_speed_; }

  void SetCyclePaused(bool paused) { cycle_paused_ = paused; }
  bool IsCyclePaused() const { return cycle_paused_; }

  // Direct time-of-day control
  // 0.0 = midnight, 0.25 = sunrise, 0.5 = noon,
  // 0.75 = sunset
  void SetTimeOfDay(float normalizedTime);
  float GetTimeOfDay() const;

  // Get time string
  std::string GetTimeString() const;

private:
  // Geometry for fullscreen quad
  struct ScreenPosVertex {
    float x, y;
    static bgfx::VertexLayout layout;
    static void initLayout() {
      if (!layout.m_stride) {
        layout.begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .end();
      }
    }
  };

  // Handles
  bgfx::ProgramHandle _skyProgram = BGFX_INVALID_HANDLE;
  bgfx::VertexBufferHandle _skyVbh = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle _skyIbh = BGFX_INVALID_HANDLE;

  // Uniforms
  bgfx::UniformHandle _sunDirUniform = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle _sunLumUniform = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle _paramsUniform = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle _viewInvUniform = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle _projInvUniform = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle _scatterParamsUniform = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle _betaRUniform = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle _betaMUniform = BGFX_INVALID_HANDLE;

  // Stored values/params
  SkyboxParams params_{};

  float cycle_speed_ = 0.05f;
  bool cycle_paused_ = false;

  // Constants
  const float H_R = 8000.0f;
  const float H_M = 1200.0f;
  const float k_betaR_per_meter[3] = {5.8e-6f, 13.5e-6f, 33.1e-6f};
  const float MieColoration[3] = {1.0f, 0.95f, 0.85f};
  const float MieBaseDensity = 20.0e-6f;
};

#endif  // SKYBOX_H