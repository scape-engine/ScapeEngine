#include "material_registry.h"

#include <glm/gtc/type_ptr.hpp>

#include "engine/core/logger.h"
#include "engine/renderer/material/standard_material.h"
#include "engine/renderer/texture/texture_registry.h"

MaterialRegistry& MaterialRegistry::Instance() {
  static MaterialRegistry instance;
  return instance;
}

MaterialHandle MaterialRegistry::Register(const Content::MaterialData& data) {
  return Register(StandardMaterial::Build(data));
}

MaterialHandle MaterialRegistry::Register(Material material) {
  MaterialHandle handle = next_handle_++;

  Logger::getInstance().Log(LogLevel::Debug,
                            "[MaterialRegistry] Registered material '" +
                                material.name + "' with handle " +
                                std::to_string(handle));

  materials_[handle] = std::move(material);
  return handle;
}

const Material* MaterialRegistry::Get(MaterialHandle handle) const {
  if (handle == INVALID_MATERIAL) {
    return nullptr;
  }

  auto it = materials_.find(handle);
  if (it != materials_.end()) {
    return &it->second;
  }

  return nullptr;
}

bgfx::ProgramHandle MaterialRegistry::Bind(MaterialHandle handle) {
  const Material* material = Get(handle);
  if (!material)
    material = &DefaultMaterial();

  for (const MaterialParam& param : material->params) {
    bgfx::setUniform(param.uniform, glm::value_ptr(param.value));
  }

  // Unset or missing textures bind their fallback
  const TextureRegistry& textures = TextureRegistry::Instance();
  for (const MaterialTexture& slot : material->textures) {
    bgfx::TextureHandle texture = textures.Get(slot.texture);
    if (!bgfx::isValid(texture))
      texture = slot.fallback;

    bgfx::setTexture(slot.stage, slot.sampler, texture);
  }

  return material->program;
}

const Material& MaterialRegistry::DefaultMaterial() {
  if (!default_material_) {
    Content::MaterialData data;
    data.name = "Default";
    data.base_color = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    data.roughness = 0.5f;
    default_material_ =
        std::make_unique<Material>(StandardMaterial::Build(data));
  }
  return *default_material_;
}

void MaterialRegistry::Unregister(MaterialHandle handle) {
  auto it = materials_.find(handle);
  if (it != materials_.end()) {
    Logger::getInstance().Log(LogLevel::Debug,
                              "[MaterialRegistry] Unregistered material '" +
                                  it->second.name + "' (handle " +
                                  std::to_string(handle) + ")");
    materials_.erase(it);
  }
}

void MaterialRegistry::Clear() {
  materials_.clear();
  default_material_.reset();
  next_handle_ = 1;
  Logger::getInstance().Log(LogLevel::Debug,
                            "[MaterialRegistry] Cleared all materials");
}
