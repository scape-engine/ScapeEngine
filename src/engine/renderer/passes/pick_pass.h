#ifndef PICK_PASS_H
#define PICK_PASS_H

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>

#include <optional>
#include <vector>

#include "engine/ecs/world.h"

// What shows at a pixel of a view (like UE's hit proxies): the entity, and
// for an InstancedMeshComponent which of its instances, and where its
// surface is. It draws the meshes under the pixel one by one, each in its own
// colour, into a one-pixel target and reads that back, so the answer comes a
// few frames later. Hidden entities and water (its own pass) don't show
class PickPass {
public:
  struct Hit {
    Entity entity = entt::null;  // null: nothing there
    // Into its InstancedMeshComponent's transforms; -1: its own mesh
    int instance = -1;
    // The surface's point and normal in the world, when something is there
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
  };

  void Create();
  void Destroy();

  // Starts a pick at `pixel` (from the top left) of a width x height view; a
  // newer one replaces one still under way
  void Request(const glm::vec2& pixel, uint16_t width, uint16_t height,
               const glm::mat4& view, const glm::mat4& projection,
               const glm::vec3& viewer);
  // The hit once the pick is read back (once); nullopt until then
  std::optional<Hit> Take();

private:
  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle skinned_program_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_pick_id_ = BGFX_INVALID_HANDLE;
  bgfx::FrameBufferHandle target_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle colour_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle read_ = BGFX_INVALID_HANDLE;
  // The surface target (view-space normal, view depth) and its read-back
  bgfx::TextureHandle surface_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle surface_read_ = BGFX_INVALID_HANDLE;

  // The pick under way: what each colour stands for (colour i + 1), the
  // frame its pixel is read by and the pixel; the view's inverse and the
  // pixel's ray in view space, to place its surface
  std::vector<Hit> drawn_;
  uint32_t ready_frame_ = 0;
  bool pending_ = false;
  uint8_t pixel_[4] = {};
  float surface_pixel_[4] = {};
  glm::mat4 inverse_view_{1.0f};
  glm::vec3 ray_from_{0.0f};
  glm::vec3 ray_{0.0f, 0.0f, 1.0f};
};

#endif  // PICK_PASS_H
