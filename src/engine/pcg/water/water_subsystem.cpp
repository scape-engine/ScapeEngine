#include "water_subsystem.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/ecs/components/compute_buffer_component.h"
#include "engine/ecs/components/compute_job_component.h"
#include "engine/ecs/components/render_texture_component.h"
#include "engine/ecs/components/shader_params_component.h"
#include "engine/ecs/components/water_surface_tag.h"
#include "engine/ecs/components/wave_field_component.h"
#include "engine/time/time.h"

namespace Water {
namespace {

constexpr uint32_t kGroupSize = 8;  // cs_wave_gerstner's threads per axis

// A grid on the xz plane, `size` across, its vertices closer in the middle
// (offsets grow with the square of the distance from it)
Geometry::MeshData Grid(float size, int resolution) {
  const int n = std::max(resolution, 2);
  Geometry::MeshData mesh;
  mesh.material_index = 0;
  mesh.vertices.reserve(size_t(n) * n);
  const auto offset = [&](int i) {
    const float u = 2.0f * float(i) / float(n - 1) - 1.0f;
    return 0.5f * size * u * std::abs(u);
  };
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i)
      mesh.vertices.emplace_back(
          glm::vec3(offset(i), 0.0f, offset(j)), glm::vec3(0.0f, 1.0f, 0.0f),
          glm::vec2(float(i), float(j)) / float(n - 1),
          glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
  mesh.indices.reserve(size_t(n - 1) * (n - 1) * 6);
  for (int j = 0; j + 1 < n; ++j)
    for (int i = 0; i + 1 < n; ++i) {
      const uint32_t a = j * n + i, b = a + n, c = a + 1, d = b + 1;
      // Facing up: cross(b - a, c - a) is +y
      mesh.indices.insert(mesh.indices.end(), {a, b, c, c, b, d});
    }
  return mesh;
}

// Cascade 0's texel width: its shortest waves span min_texels texels
float FirstTexel(const Worldgen::WaveCascadeSettings& s) {
  return s.base_scale * 4.0f / float(std::max(s.resolution, 1));
}

}  // namespace

void Subsystem::Make(ECS& ecs, Entity entity,
                     std::shared_ptr<const Worldgen::WaterWaveSettings> waves,
                     float radius, const SurfaceSettings& settings) {
  Surface& surface = surfaces_[entity];
  Release(surface);
  surface.mesh = std::make_unique<Mesh>(
      Grid(settings.patch_size, settings.grid_resolution));
  surface.material =
      MaterialRegistry::Instance().Register(WaterMaterial::Build(settings.look));

  const Worldgen::WaveCascadeSettings& cascades = settings.cascades;
  const auto resolution =
      static_cast<uint16_t>(std::max(cascades.resolution, 1));
  const auto count = static_cast<uint16_t>(std::max(cascades.count, 1));

  ecs.Reg().emplace<WaterSurfaceTag>(entity);  // empty: Add returns nothing
  auto& field = ecs.Add<WaveFieldComponent>(entity);
  field.waves = std::move(waves);
  field.cascades = cascades;
  field.radius = radius;

  auto& renderer = ecs.Add<MeshRendererComponent>(entity);
  renderer.mesh_ = surface.mesh.get();
  renderer.material_ = surface.material;

  // Must match cs_wave_gerstner.sc and water_waves.sh
  auto& textures = ecs.Add<RenderTextureComponent>(entity);
  textures.textures_.push_back({"s_waveDisplacement", resolution, resolution,
                                count, bgfx::TextureFormat::RGBA16F});
  auto& buffers = ecs.Add<ComputeBufferComponent>(entity);
  buffers.buffers_.push_back({"cascades"});
  buffers.buffers_.push_back({"waves"});
  auto& job = ecs.Add<ComputeJobComponent>(entity);
  const uint32_t groups = (resolution + kGroupSize - 1) / kGroupSize;
  job.dispatches_.push_back(
      {"cs_wave_gerstner.bin", {0, 1}, {0}, {groups, groups, count}});
  auto& params = ecs.Add<ShaderParamsComponent>(entity);
  params.uniforms_.push_back(
      {"u_waterWaves", {glm::vec4(0.0f), glm::vec4(0.0f)}});
}

void Subsystem::Update(ECS& ecs) {
  // Surfaces whose entity is gone
  for (auto it = surfaces_.begin(); it != surfaces_.end();) {
    if (!ecs.Reg().valid(it->first) ||
        !ecs.Has<WaveFieldComponent>(it->first)) {
      Release(it->second);
      it = surfaces_.erase(it);
    } else {
      ++it;
    }
  }

  const float dt = Time::Deltaf();
  const auto time_ms = static_cast<uint64_t>(Time::World() * 1000.0);
  for (auto [entity, transform, wave, buffers, params] :
       ecs.View<TransformComponent, WaveFieldComponent, ComputeBufferComponent,
                ShaderParamsComponent>()
           .each()) {
    if (!wave.waves || buffers.buffers_.size() < 2 || params.uniforms_.empty())
      continue;
    Worldgen::UpdateWaves(*wave.waves, dt, wave.field);
    std::vector<Worldgen::Wave> sorted = wave.field.waves;
    Worldgen::SortWaves(sorted);

    // The point under the viewer, in the parent's space, and the surface's
    // axes there
    const glm::mat4 parent =
        ecs.Reg().valid(transform.parent_) &&
                ecs.Has<TransformComponent>(transform.parent_)
            ? ecs.Get<TransformComponent>(transform.parent_).model_
            : glm::mat4(1.0f);
    const glm::vec3 viewer = glm::inverse(parent) * glm::vec4(viewer_, 1.0f);
    glm::vec3 up(0.0f, 1.0f, 0.0f), origin(viewer.x, 0.0f, viewer.z);
    if (wave.radius > 0.0f) {
      const float length = glm::length(viewer);
      if (length > 0.0f)
        up = viewer / length;
      origin = up * wave.radius;
    }
    const glm::vec3 ref = std::abs(up.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f)
                                                : glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 tangent_x = glm::normalize(ref - up * glm::dot(ref, up));
    const glm::vec3 tangent_z = glm::cross(tangent_x, up);

    Worldgen::WaveFrame frame;
    for (int i = 0; i < 3; ++i) {
      frame.tangent_x[i] = tangent_x[i];
      frame.tangent_z[i] = tangent_z[i];
      frame.up[i] = up[i];
    }
    frame.radius = wave.radius;
    frame.time_ms = time_ms;
    const Worldgen::WaveCascades cascades =
        Worldgen::BuildWaveCascades(sorted, wave.cascades, frame);

    // Per cascade and one past: first group, texel width, slope variance
    const float texel = FirstTexel(wave.cascades);
    auto& layers = buffers.buffers_[0].data;
    layers.resize(cascades.starts.size());
    for (size_t c = 0; c < layers.size(); ++c)
      layers[c] = glm::vec4(float(cascades.starts[c]),
                            texel * std::ldexp(1.0f, int(c)),
                            cascades.slope_variance[c], 0.0f);
    buffers.buffers_[0].dirty = true;
    // Seven vec4s per group of four waves, as GerstnerGroup lays them out
    static_assert(sizeof(Worldgen::GerstnerGroup) == 7 * sizeof(glm::vec4));
    auto& groups = buffers.buffers_[1].data;
    groups.assign(std::max<size_t>(cascades.groups.size() * 7, 1),
                  glm::vec4(0.0f));
    if (!cascades.groups.empty())
      std::memcpy(groups.data(), cascades.groups.data(),
                  cascades.groups.size() * sizeof(Worldgen::GerstnerGroup));
    buffers.buffers_[1].dirty = true;

    params.uniforms_[0].values = {
        glm::vec4(texel, float(std::max(wave.cascades.count, 1)), wave.radius,
                  float(std::max(wave.cascades.resolution, 1))),
        glm::vec4(wave.depth, 0.0f, 0.0f, 0.0f)};

    const glm::quat rotation =
        glm::quat_cast(glm::mat3(tangent_x, up, tangent_z));
    transform.position_ = origin;
    transform.rotation_ = rotation;
    transform.euler_angles_ = glm::degrees(glm::eulerAngles(rotation));
    transform.modified_ = true;
  }
}

void Subsystem::Shutdown() {
  for (auto& [entity, surface] : surfaces_)
    Release(surface);
  surfaces_.clear();
}

void Subsystem::Release(Surface& surface) {
  if (surface.material != INVALID_MATERIAL)
    MaterialRegistry::Instance().Unregister(surface.material);
  surface = Surface();
}

}  // namespace Water
