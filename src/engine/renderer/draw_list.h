#ifndef DRAW_LIST_H
#define DRAW_LIST_H

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>

#include <vector>

#include "engine/ecs/world.h"
#include "engine/renderer/material/material_registry.h"

class Mesh;

// The frame's meshes, grouped for drawing: meshes sharing a mesh and a
// material that has an instanced program are drawn once per pass, their model
// matrices in an instance buffer; the rest are drawn one by one
struct DrawList {
  struct Batch {
    const Mesh* mesh;
    MaterialHandle material;
    bgfx::InstanceDataBuffer instances;  // valid for the frame it was built
  };
  struct Single {
    Entity entity;
    const glm::mat4* model;
    const Mesh* mesh;
    MaterialHandle material;
  };

  std::vector<Batch> batches;
  std::vector<Single> singles;

  // Groups the render queue's enabled meshes inside the `view_projection`
  // frustum, once per frame after the transforms are evaluated. `apart`
  // entities always draw one by one (e.g. the selection, which passes skip or
  // draw differently), as everything does without `instanced`
  void Build(const std::vector<Entity>& apart,
             const glm::mat4& view_projection, bool instanced = true);

  // Binds the batch's instances and mesh for the next submit
  static void Bind(const Batch& batch);
};

#endif  // DRAW_LIST_H
