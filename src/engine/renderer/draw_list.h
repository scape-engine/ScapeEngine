#ifndef DRAW_LIST_H
#define DRAW_LIST_H

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>

#include <initializer_list>
#include <vector>

#include "engine/ecs/world.h"
#include "engine/renderer/material/material_registry.h"

class Mesh;

// The frame's meshes, grouped for drawing: meshes sharing a mesh and a
// material that has an instanced program are drawn once per pass, their model
// matrices in an instance buffer, as is each InstancedMeshComponent part (from
// the component's buffer); the rest are drawn one by one
struct DrawList {
  // How much of a draw the standard shaders show (dithered; fade.sh): it
  // fades out from `start` to `end` from the viewer (end <= start: it
  // doesn't), times `visibility` (FadeSystem::Visibility)
  struct Fade {
    glm::vec3 viewer{0.0f};
    float start = 0.0f;
    float end = 0.0f;
    float visibility = 1.0f;
  };
  struct Batch {
    const Mesh* mesh;
    MaterialHandle material;
    bgfx::InstanceDataBuffer instances;  // valid for the frame it was built
    // Else an InstancedMeshComponent's buffer and the part's run of it
    bgfx::VertexBufferHandle buffer = BGFX_INVALID_HANDLE;
    uint32_t first = 0;
    uint32_t count = 0;
    bool mirrored = false;  // its matrices turn triangles over
    Fade fade;  // each instance fades by its own origin
  };
  struct Single {
    Entity entity;
    const glm::mat4* model;
    const Mesh* mesh;
    MaterialHandle material;
    // A skinned mesh's pose (its animator's) and node count; null: none
    const glm::mat4* pose;
    uint32_t pose_count;
    Fade fade = {};
  };

  std::vector<Batch> batches;
  std::vector<Single> singles;

  // A list to fill with what its view-projection sees
  struct Frustum {
    DrawList* list;
    glm::mat4 view_projection;
  };

  // Fills each list with the render queue's enabled meshes inside its frustum,
  // in one pass over the scene, once per frame after the transforms are
  // evaluated. `apart` entities always draw one by one (e.g. the selection,
  // which passes skip or draw differently), as everything does without
  // `instanced`, and anything partly faded. Instanced parts draw only as far
  // from the viewer as they allow, each instance as its mesh's LOD for its
  // distance; hidden entities don't draw
  static void Build(std::initializer_list<Frustum> frusta,
                    const glm::vec3& viewer, const std::vector<Entity>& apart,
                    bool instanced = true);

  // Binds the batch's instances, its fade and its mesh for the next submit
  static void Bind(const Batch& batch);
  // Sets the fade of the next submit with the standard shaders: every draw
  // with them sets one (bgfx keeps a uniform's last value)
  static void SetFade(const Fade& fade);

  // The material's blend mode (Opaque for a missing one): depth passes draw
  // only Opaque, the scene draws Blend and Additive after the rest
  static Content::MaterialBlend BlendOf(MaterialHandle material);

  // Whether a matrix mirrors (turns triangles over: a negative determinant)
  static bool Mirrored(const glm::mat4& model);
  // The cull state for a material drawn mirrored or not: none for a
  // double-sided one, else the back faces (front ones when mirrored)
  static uint64_t Cull(MaterialHandle material, bool mirrored);

  // Sets u_bones from a skinned mesh's pose for the next submit; false when
  // the mesh isn't posed (draw it unskinned)
  static bool SetBones(const Mesh& mesh, const glm::mat4* pose,
                       uint32_t pose_count);
};

#endif  // DRAW_LIST_H
