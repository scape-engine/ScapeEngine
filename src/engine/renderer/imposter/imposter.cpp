#include "imposter.h"

#include <algorithm>
#include <cfloat>
#include <deque>

#include <bx/math.h>
#include <glm/gtc/type_ptr.hpp>

#include "engine/context/engine_context.h"
#include "engine/core/view_ids.h"
#include "engine/renderer/draw_list.h"
#include "engine/renderer/graphics_renderer.h"
#include "engine/renderer/model/mesh.h"

namespace {

// Waiting to bake, oldest first, with the frame each was queued in
struct Queued {
  Imposter* imposter;
  uint64_t frame;
};
std::deque<Queued> g_queue;
uint64_t g_frame = 0;  // counts BakeNext's calls

// A baked view's camera axes, as the card shader turns the card: up is the
// model's y, or z looking straight down
void ViewAxes(const glm::vec3& d, glm::vec3& right, glm::vec3& up) {
  right = glm::cross(d, glm::vec3(0.0f, 1.0f, 0.0f));
  if (glm::dot(right, right) < 1e-8f)
    right = glm::cross(d, glm::vec3(0.0f, 0.0f, 1.0f));
  right = glm::normalize(right);
  up = glm::cross(-d, right);
}

}  // namespace

glm::vec3 Imposter::ViewDirection(int i, int j, int views) {
  // The tile's centre on the hemi-octahedron (vs_imposter.sc encodes it)
  const glm::vec2 o =
      (glm::vec2(i + 0.5f, j + 0.5f) / static_cast<float>(views)) * 2.0f -
      1.0f;
  const glm::vec2 p((o.x + o.y) * 0.5f, (o.x - o.y) * 0.5f);
  return glm::normalize(
      glm::vec3(p.x, 1.0f - std::abs(p.x) - std::abs(p.y), p.y));
}

Imposter::Imposter(std::vector<Variant> variants, int resolution, int views)
    : variants_(std::move(variants)),
      resolution_(std::clamp(resolution, 64, 4096)),
      views_(std::clamp(views, 2, 32)) {
  // A sphere around every variant's parts
  glm::vec3 low(FLT_MAX), high(-FLT_MAX);
  struct Sphere {
    glm::vec3 center;
    float radius;
  };
  std::vector<Sphere> spheres;
  for (const Variant& variant : variants_)
    for (const Part& part : variant) {
      const glm::mat4& m = part.matrix;
      const float scale = std::sqrt(std::max(
          {glm::dot(m[0], m[0]), glm::dot(m[1], m[1]), glm::dot(m[2], m[2])}));
      const Sphere sphere{glm::vec3(m * glm::vec4(part.mesh->BoundsCenter(), 1.0f)),
                          part.mesh->BoundsRadius() * scale};
      low = glm::min(low, sphere.center);
      high = glm::max(high, sphere.center);
      spheres.push_back(sphere);
    }
  if (spheres.empty())
    return;
  center_ = (low + high) * 0.5f;
  for (const Sphere& sphere : spheres)
    radius_ = std::max(radius_,
                       glm::distance(center_, sphere.center) + sphere.radius);

  // The atlas: the variants side by side, each a grid of views
  const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                         BGFX_SAMPLER_V_CLAMP;
  const uint16_t width =
      static_cast<uint16_t>(resolution_ * static_cast<int>(variants_.size()));
  const uint16_t height = static_cast<uint16_t>(resolution_);
  albedo_ = bgfx::createTexture2D(width, height, true, 1,
                                  bgfx::TextureFormat::RGBA8, flags);
  normal_ = bgfx::createTexture2D(width, height, true, 1,
                                  bgfx::TextureFormat::RGBA8, flags);
  const bgfx::TextureHandle depth = bgfx::createTexture2D(
      width, height, false, 1, bgfx::TextureFormat::D24S8,
      BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY);
  bgfx::Attachment attachments[3];
  attachments[0].init(albedo_);  // mips made on resolve
  attachments[1].init(normal_);
  attachments[2].init(depth, bgfx::Access::Write, 0, 1, 0, BGFX_RESOLVE_NONE);
  target_ = bgfx::createFrameBuffer(3, attachments, false);

  // The card: a square around the sphere, its corners as texture coordinates
  Geometry::MeshData quad;
  for (const glm::vec2 corner : {glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, -1.0f),
                                 glm::vec2(1.0f, 1.0f), glm::vec2(-1.0f, 1.0f)})
    quad.vertices.emplace_back(
        center_ + glm::vec3(corner, 0.0f) * radius_, glm::vec3(0.0f, 0.0f, 1.0f),
        corner, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
  quad.indices = {0, 1, 2, 0, 2, 3};
  quad.material_index = 0;
  card_ = std::make_unique<Mesh>(quad);

  auto& graphics = static_cast<GraphicsRenderer&>(EngineContext::Renderer());
  ::Material material;
  material.instanced_program = EngineContext::Shader().LoadProgram(
      "imposter", "vs_imposter.bin", "fs_imposter.bin");
  material.pre_pass_program = EngineContext::Shader().LoadProgram(
      "imposter", "vs_imposter.bin", "fs_imposter_depth.bin");
  material.shadow_program = EngineContext::Shader().LoadProgram(
      "imposter", "vs_imposter.bin", "fs_imposter_shadow.bin");
  material.params = {
      {graphics.CreateUniform("u_imposter", bgfx::UniformType::Vec4),
       glm::vec4(static_cast<float>(views_),
                 static_cast<float>(variants_.size()), radius_,
                 bgfx::getCaps()->originBottomLeft ? 1.0f : 0.0f)},
      {graphics.CreateUniform("u_imposterCenter", bgfx::UniformType::Vec4),
       glm::vec4(center_, 0.0f)}};
  // The atlas is no registered texture: it binds as the slots' fallback
  material.textures = {
      {graphics.CreateUniform("s_imposterAlbedo", bgfx::UniformType::Sampler),
       0, INVALID_TEXTURE, albedo_},
      {graphics.CreateUniform("s_imposterNormal", bgfx::UniformType::Sampler),
       1, INVALID_TEXTURE, normal_}};
  material.double_sided = true;  // turned to the eye, mirrored or not
  for (size_t v = 0; v < variants_.size(); ++v) {
    material.name = "Imposter " + std::to_string(v);
    material.params[1].value.w = static_cast<float>(v);
    materials_.push_back(MaterialRegistry::Instance().Register(material));
  }

  g_queue.push_back({this, g_frame});
}

Imposter::~Imposter() {
  g_queue.erase(std::remove_if(g_queue.begin(), g_queue.end(),
                               [this](const Queued& queued) {
                                 return queued.imposter == this;
                               }),
                g_queue.end());
  for (MaterialHandle material : materials_)
    MaterialRegistry::Instance().Unregister(material);
  // Destroys the depth buffer it owns; the atlas textures are kept apart
  if (bgfx::isValid(target_))
    bgfx::destroy(target_);
  for (bgfx::TextureHandle texture : {albedo_, normal_})
    if (bgfx::isValid(texture))
      bgfx::destroy(texture);
}

void Imposter::BakeNext() {
  // Not in the frame its atlas was made: the bake would be lost
  const uint64_t frame = g_frame++;
  if (g_queue.empty() || g_queue.front().frame == frame)
    return;
  Imposter* imposter = g_queue.front().imposter;
  g_queue.pop_front();
  imposter->Bake();
}

void Imposter::Bake() {
  if (!bgfx::isValid(target_))
    return;
  static const bgfx::ProgramHandle program = EngineContext::Shader().LoadProgram(
      "imposter", "vs_imposter_bake.bin", "fs_imposter_bake.bin");
  auto& graphics = static_cast<GraphicsRenderer&>(EngineContext::Renderer());
  static const bgfx::UniformHandle view_projection =
      graphics.CreateUniform("u_bakeViewProj", bgfx::UniformType::Mat4);
  static const bgfx::UniformHandle eye =
      graphics.CreateUniform("u_bakeEye", bgfx::UniformType::Vec4);
  const bgfx::Caps* caps = bgfx::getCaps();

  const uint16_t width =
      static_cast<uint16_t>(resolution_ * static_cast<int>(variants_.size()));
  const uint16_t height = static_cast<uint16_t>(resolution_);
  const uint8_t view = ViewID::IMPOSTER_BAKE;
  bgfx::setViewFrameBuffer(view, target_);
  bgfx::setViewRect(view, 0, 0, width, height);
  bgfx::setViewClear(view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x00000000,
                     1.0f, 0);
  bgfx::touch(view);

  // An orthographic camera on the sphere, squeezed into each view's tile
  float ortho[16];
  bx::mtxOrtho(ortho, -radius_, radius_, -radius_, radius_, 0.0f,
               radius_ * 2.02f, 0.0f, caps->homogeneousDepth,
               bx::Handedness::Left);
  const glm::mat4 projection = glm::make_mat4(ortho);
  const int columns = views_ * static_cast<int>(variants_.size());
  // The scene's projection flips y where the origin is top left
  // (Transformation::Projection), and DrawList::Cull's sides suit it; this one
  // doesn't, so its triangles wind the other way there
  const bool mirrored = !caps->originBottomLeft;
  const uint16_t tile = static_cast<uint16_t>(resolution_ / views_);
  for (size_t v = 0; v < variants_.size(); ++v)
    for (int i = 0; i < views_; ++i)
      for (int j = 0; j < views_; ++j) {
        const glm::vec3 d = ViewDirection(i, j, views_);
        glm::vec3 right, up;
        ViewAxes(d, right, up);
        const glm::vec3 position = center_ + d * (radius_ * 1.01f);
        glm::mat4 camera(1.0f);
        for (int k = 0; k < 3; ++k) {
          camera[k][0] = right[k];
          camera[k][1] = up[k];
          camera[k][2] = -d[k];
        }
        camera[3] = glm::vec4(-glm::dot(right, position),
                              -glm::dot(up, position),
                              glm::dot(d, position), 1.0f);

        // Column c and row j of the atlas; rows count from the texture's
        // origin (vs_imposter.sc reads them so)
        const int c = static_cast<int>(v) * views_ + i;
        const float rows = static_cast<float>(views_);
        glm::mat4 squeeze(1.0f);
        squeeze[0][0] = 1.0f / static_cast<float>(columns);
        squeeze[1][1] = 1.0f / rows;
        squeeze[3][0] = (2.0f * c + 1.0f) / static_cast<float>(columns) - 1.0f;
        squeeze[3][1] = caps->originBottomLeft
                            ? (2.0f * j + 1.0f) / rows - 1.0f
                            : 1.0f - (2.0f * j + 1.0f) / rows;
        const glm::mat4 bake = squeeze * projection * camera;
        const uint16_t x = static_cast<uint16_t>(c * tile);
        const uint16_t y = static_cast<uint16_t>(
            caps->originBottomLeft ? height - (j + 1) * tile : j * tile);
        const glm::vec4 towards(d, 0.0f);

        for (const Part& part : variants_[v]) {
          bgfx::setTransform(glm::value_ptr(part.matrix));
          part.mesh->Bind();
          MaterialRegistry::Instance().Bind(part.material);
          bgfx::setUniform(view_projection, glm::value_ptr(bake));
          bgfx::setUniform(eye, glm::value_ptr(towards));
          bgfx::setScissor(x, y, tile, tile);
          bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                         BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                         DrawList::Cull(part.material,
                                        DrawList::Mirrored(part.matrix) !=
                                            mirrored));
          bgfx::submit(view, program);
        }
      }
  baked_ = true;
}
