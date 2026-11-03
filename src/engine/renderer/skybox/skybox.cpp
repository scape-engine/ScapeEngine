#include "engine/renderer/skybox/skybox.h"

#include <cstring>

#include "engine/context/engine_context.h"
#include "engine/renderer/graphics_renderer.h"
#include "engine/resources/shader_utils.h"

// Static member definition
bgfx::VertexLayout Skybox::ScreenPosVertex::layout;

// ---- helpers ----
// TODO: check that functions are like in GameTest
static inline float local_min(float a, float b) {
  return a < b ? a : b;
}
static inline float local_max(float a, float b) {
  return a > b ? a : b;
}
static inline float local_clamp(float v, float m, float M) {
  return local_max(m, local_min(v, M));
}
static inline float local_smoothStep(float e0, float e1, float x) {
  float t = local_clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// ---------------------------------------------------------------
// Create
// ---------------------------------------------------------------
void Skybox::Create(ShaderManager* ShaderManager) {
  // Program
  _skyProgram = ShaderManager->LoadProgram("skybox_proc", "vs_skybox.bin",
                                           "fs_skybox.bin");

  // TODO: check that names are like in GameTest
  // Move
  // Uniforms
  // TODO: move uniform ownership to GraphicsRenderer
  _sunDirUniform =
      bgfx::createUniform("u_sunDirection", bgfx::UniformType::Vec4);
  _sunLumUniform =
      bgfx::createUniform("u_sunLuminance", bgfx::UniformType::Vec4);
  _paramsUniform = bgfx::createUniform("u_parameters", bgfx::UniformType::Vec4);
  _scatterParamsUniform =
      bgfx::createUniform("u_scatterParams", bgfx::UniformType::Vec4);
  _betaRUniform = bgfx::createUniform("u_betaR", bgfx::UniformType::Vec4);
  _betaMUniform = bgfx::createUniform("u_betaM", bgfx::UniformType::Vec4);
  _viewInvUniform = bgfx::createUniform("u_viewInv", bgfx::UniformType::Mat4);
  _projInvUniform = bgfx::createUniform("u_projInv", bgfx::UniformType::Mat4);

  // Geometry
  CreateFullscreenQuad();

  // Initialize constant scattering totals
  params_.totalBetaR[0] = k_betaR_per_meter[0];
  params_.totalBetaR[1] = k_betaR_per_meter[1];
  params_.totalBetaR[2] = k_betaR_per_meter[2];
  params_.totalBetaR[3] = 0.0f;

  float mieFactor = 1.0f;

  params_.totalBetaM[0] = MieBaseDensity * MieColoration[0] * mieFactor;
  params_.totalBetaM[1] = MieBaseDensity * MieColoration[1] * mieFactor;
  params_.totalBetaM[2] = MieBaseDensity * MieColoration[2] * mieFactor;
  params_.totalBetaM[3] = 0.0f;

  // Scattering Parameters
  params_.scatterParams[0] = 0.85f;
  params_.scatterParams[1] = 0.0f;
  params_.scatterParams[2] = 0.0f;
  params_.scatterParams[3] = 0.0f;

  // Default UI parameters, time is kept in params_._cycleTime
  SetParams(/*sunAngularRadius*/ 0.0465f, /*bloom*/ 1.0f, /*exposure*/ 0.25f,
            /*mie*/ 0.76f);

  // Initialize ambient to a dark color
  params_._skyAmbientArray[0] = 0.1f;
  params_._skyAmbientArray[1] = 0.1f;
  params_._skyAmbientArray[2] = 0.1f;
  params_._skyAmbientArray[3] = 0.0f;
}

// ---------------------------------------------------------------
// Destroy
// ---------------------------------------------------------------
void Skybox::Destroy() {
  auto destroy = [](auto& h) {
    if (bgfx::isValid(h)) {
      bgfx::destroy(h);
      h = BGFX_INVALID_HANDLE;
    }
  };

  destroy(_skyProgram);
  destroy(_skyVbh);
  destroy(_skyIbh);

  destroy(_sunDirUniform);
  destroy(_sunLumUniform);
  destroy(_paramsUniform);
  destroy(_viewInvUniform);
  destroy(_projInvUniform);
  destroy(_scatterParamsUniform);
  destroy(_betaRUniform);
  destroy(_betaMUniform);
}

// ---------------------------------------------------------------
// Update (advance time + recompute)
// ---------------------------------------------------------------
void Skybox::Update(float dt) {
  if (!cycle_paused_) {
    params_._cycleTime += dt * cycle_speed_;
    if (params_._cycleTime > bx::kPi * 2.0f) {
      params_._cycleTime -= bx::kPi * 2.0f;
    }
  }
  ComputeSun();
}

// ---------------------------------------------------------------
// Day/night control
// ---------------------------------------------------------------
void Skybox::SetTimeOfDay(float normalizedTime) {
  // Clamp to [0, 1] and convert to radians
  normalizedTime = local_clamp(normalizedTime, 0.0f, 1.0f);
  params_._cycleTime = normalizedTime * bx::kPi * 2.0f;
  ComputeSun();
}

float Skybox::GetTimeOfDay() const {
  return params_._cycleTime / (bx::kPi * 2.0f);
}

std::string Skybox::GetTimeString() const {
  // Convert radians to 24-hour time
  float normalized = GetTimeOfDay();
  int hours = static_cast<int>(normalized * 24.0f) % 24;
  int minutes = static_cast<int>((normalized * 24.0f - hours) * 60.0f);

  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", hours, minutes);
  return std::string(buffer);
}

// ---------------------------------------------------------------
// SetParams
// ---------------------------------------------------------------
void Skybox::SetParams(float sunAngularRadius, float proceduralBloom,
                       float exposure, float mieAnisotropy) {
  // 1. Sun & Exposure Params
  params_._parametersArray[0] = sunAngularRadius;
  params_._parametersArray[1] = proceduralBloom;
  params_._parametersArray[2] = exposure;
  // .w (time) is updated in ComputeSun(), so not changing

  // 2. Atmosphere Params
  // This maps to u_scatterParams.x in the shader
  params_.scatterParams[0] = mieAnisotropy;
}

// ---------------------------------------------------------------
// ComputeSun
// ---------------------------------------------------------------
void Skybox::ComputeSun() {
  const float sunAngle = params_._cycleTime;
  bx::Vec3 sunDirectionVec =
      bx::normalize(bx::Vec3(bx::cos(sunAngle), bx::sin(sunAngle), 0.1f));

  if (sunDirectionVec.y < -0.2f)
    sunDirectionVec.y = -0.2f;  // Clamp min height

  params_.sunDirShader[0] = sunDirectionVec.x;
  params_.sunDirShader[1] = sunDirectionVec.y;
  params_.sunDirShader[2] = sunDirectionVec.z;
  params_.sunDirShader[3] = 0.0f;

  float sunElevationFactor = local_smoothStep(-0.15f, 0.2f, sunDirectionVec.y);

  // const float baseSunLuminance = 10.0f;
  // we don't need this for Nishita Model

  const float sunIntensity = 125.0f;

  float dayNightMask = local_smoothStep(-0.2f, 0.0f, sunDirectionVec.y);

  params_._sunColorArray[0] = 1.0f * sunIntensity * dayNightMask;  // Red
  params_._sunColorArray[1] = 1.0f * sunIntensity * dayNightMask;  // Green
  params_._sunColorArray[2] = 1.0f * sunIntensity * dayNightMask;  // Blue
  params_._sunColorArray[3] = 1.0f;

  // parameters.w holds time
  params_._parametersArray[3] = params_._cycleTime;

  // Ambient from elevation + tiny boost
  params_._skyAmbientArray[0] = bx::lerp(0.02f, 0.3f, sunElevationFactor);
  params_._skyAmbientArray[1] = bx::lerp(0.03f, 0.4f, sunElevationFactor);
  params_._skyAmbientArray[2] = bx::lerp(0.05f, 0.55f, sunElevationFactor);
  params_._skyAmbientArray[3] = 0.0f;

  /* const float ambientBoost = 0.005f;
  for (int i = 0; i < 3; ++i)
    params_._skyAmbientArray[i] *= ambientBoost;*/
}

// ---------------------------------------------------------------
// Submit (draw skybox)
// ---------------------------------------------------------------
void Skybox::Submit(bgfx::ViewId viewId, const float* viewMatrix,
                    const float* projMatrix, bool useInverseViewProj,
                    bool capture) {
  if (!bgfx::isValid(_skyProgram) || !bgfx::isValid(_skyVbh) ||
      !bgfx::isValid(_skyIbh))
    return;

  // Set view/proj for the target view
  bgfx::setViewTransform(viewId, viewMatrix, projMatrix);

  // Model = identity (fullscreen quad)
  float model[16];
  bx::mtxIdentity(model);
  bgfx::setTransform(model);

  // Geometry
  bgfx::setVertexBuffer(0, _skyVbh);
  bgfx::setIndexBuffer(_skyIbh);

  // View/proj uniforms
  if (useInverseViewProj) {
    float invView[16], invProj[16];
    bx::mtxInverse(invView, viewMatrix);
    bx::mtxInverse(invProj, projMatrix);
    bgfx::setUniform(_viewInvUniform, invView);
    bgfx::setUniform(_projInvUniform, invProj);
  } else {
    // Reflection path: pass rotation-only "view" directly,
    // and the non-inverted projection into the *Inv uniforms*.
    bgfx::setUniform(_viewInvUniform, viewMatrix);
    bgfx::setUniform(_projInvUniform, projMatrix);
  }

  // Sky/sun uniforms
  bgfx::setUniform(_sunDirUniform, params_.sunDirShader);
  bgfx::setUniform(_sunLumUniform, params_._sunColorArray);
  bgfx::setUniform(_paramsUniform, params_._parametersArray);
  bgfx::setUniform(_betaRUniform, params_.totalBetaR);
  bgfx::setUniform(_betaMUniform, params_.totalBetaM);
  bgfx::setUniform(_scatterParamsUniform, params_.scatterParams);

  // Display: tonemapped with sun disk; capture: linear, no sun disk
  const float output[4] = {capture ? 0.0f : 1.0f, capture ? 0.0f : 1.0f, 0.0f,
                           0.0f};
  auto& graphics = static_cast<GraphicsRenderer&>(EngineContext::Renderer());
  bgfx::setUniform(graphics.GetSkyOutputUniform(), output);

  // Capture targets have no depth attachment
  uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
  if (!capture) {
    state |= BGFX_STATE_DEPTH_TEST_LEQUAL;
  }

  bgfx::setState(state);

  bgfx::submit(viewId, _skyProgram);
}

// ---------------------------------------------------------------
// CreateFullscreenQuad
// ---------------------------------------------------------------
void Skybox::CreateFullscreenQuad() {
  ScreenPosVertex::initLayout();

  static const ScreenPosVertex quadVertices[] = {
      {-1.0f, -1.0f},
      {1.0f, -1.0f},
      {-1.0f, 1.0f},
      {1.0f, 1.0f},
  };

  static const uint16_t quadIndices[] = {0, 2, 1, 1, 2, 3};

  _skyVbh = bgfx::createVertexBuffer(
      bgfx::makeRef(quadVertices, sizeof(quadVertices)),
      ScreenPosVertex::layout);
  _skyIbh =
      bgfx::createIndexBuffer(bgfx::makeRef(quadIndices, sizeof(quadIndices)));
}
