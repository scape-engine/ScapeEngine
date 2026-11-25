#ifndef IMPOSTER_H
#define IMPOSTER_H

#include <memory>
#include <vector>

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>

#include "engine/renderer/material/material_registry.h"

class Mesh;

// A model drawn far away as a card turned to the eye (an imposter): its look
// from a grid of directions over its upper hemisphere (hemi-octahedral),
// baked into an atlas of colour and normals for each of a few variants of it.
// The card is a mesh to draw, instanced, at the model's transforms, with a
// material per variant; each instance shows the view nearest its direction to
// the eye (imposter shaders). Baking takes a frame per variant on the GPU:
// the oldest queued imposter bakes its next variant each frame (BakeNext)
class Imposter {
public:
  // A part of a variant: a mesh and material at a transform in the model
  struct Part {
    const Mesh* mesh;
    MaterialHandle material;
    glm::mat4 matrix;
  };
  using Variant = std::vector<Part>;

  // `resolution` pixels a side for each variant, `views` x `views` views; the
  // meshes and materials must outlive the bake
  Imposter(std::vector<Variant> variants, int resolution, int views);
  ~Imposter();
  Imposter(const Imposter&) = delete;
  Imposter& operator=(const Imposter&) = delete;

  // Whether it has baked: its card draws only then
  bool Baked() const { return baked_; }
  const Mesh* Card() const { return card_.get(); }
  // The card's material showing `variant`
  MaterialHandle Material(size_t variant) const { return materials_[variant]; }
  const std::vector<Variant>& Variants() const { return variants_; }

  // Bakes the oldest queued imposter's next variant, once a frame before the
  // views that draw them (ViewID::IMPOSTER_BAKE)
  static void BakeNext();

  // The direction (model space) of view (i, j) on a grid of `views` a side
  static glm::vec3 ViewDirection(int i, int j, int views);

private:
  void Bake();

  std::vector<Variant> variants_;
  int resolution_;
  int views_;
  glm::vec3 center_{0.0f};  // a sphere around every variant
  float radius_ = 0.0f;
  size_t next_variant_ = 0;  // the one to bake next
  bool baked_ = false;

  bgfx::TextureHandle albedo_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle normal_ = BGFX_INVALID_HANDLE;
  bgfx::FrameBufferHandle target_ = BGFX_INVALID_HANDLE;
  std::unique_ptr<Mesh> card_;
  std::vector<MaterialHandle> materials_;  // by variant
};

#endif  // IMPOSTER_H
