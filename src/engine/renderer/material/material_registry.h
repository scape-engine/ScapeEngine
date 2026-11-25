#ifndef MATERIAL_REGISTRY_H
#define MATERIAL_REGISTRY_H

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "engine/core/types/material_data.h"
#include "engine/renderer/material/material.h"
#include "engine/renderer/texture/texture_registry.h"

using MaterialHandle = uint32_t;
constexpr MaterialHandle INVALID_MATERIAL = 0;

class MaterialRegistry {
public:
  static MaterialRegistry& Instance();

  // Decode the textures of many materials at once (in parallel), safe on
  // any thread. Upload the result with TextureRegistry::Upload before
  // registering the materials one by one
  std::vector<TextureRegistry::PendingTexture> PrepareTextures(
      const std::vector<Content::MaterialData>& materials) const;

  // Register an imported material as a standard (PBR) material
  MaterialHandle Register(const Content::MaterialData& data);

  // Register any material, e.g. one using a custom shader
  MaterialHandle Register(Material material);

  // Get material by handle (returns nullptr if invalid)
  const Material* Get(MaterialHandle handle) const;

  // Set the material's params and textures for the next draw, and return
  // the program to submit with. Invalid handles use a default material
  bgfx::ProgramHandle Bind(MaterialHandle handle);

  // Whether back faces of the material must be drawn (no culling)
  bool IsDoubleSided(MaterialHandle handle) const;

  // Unregister a material by handle
  void Unregister(MaterialHandle handle);

  // Clear all materials
  void Clear();

private:
  MaterialRegistry() = default;

  // Grey standard material, for meshes without a valid material
  const Material& DefaultMaterial();

  std::unordered_map<MaterialHandle, Material> materials_;
  std::unique_ptr<Material> default_material_;
  MaterialHandle next_handle_{1};
};

#endif  // MATERIAL_REGISTRY_H
