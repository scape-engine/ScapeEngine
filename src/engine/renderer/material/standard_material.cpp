#include "standard_material.h"

#include "engine/context/engine_context.h"
#include "engine/renderer/graphics_renderer.h"

Material StandardMaterial::Build(const Content::MaterialData& data) {
  auto& graphics = static_cast<GraphicsRenderer&>(EngineContext::Renderer());
  auto& registry = TextureRegistry::Instance();

  Material material;
  material.name = data.name;
  material.program = EngineContext::Shader().LoadProgram(
      "materials", "vs_lit.bin", "fs_lit.bin");

  // Factors, multiplied with the textures in the shader (fs_lit.sc)
  const auto param = [&](const char* name, const glm::vec4& value) {
    return MaterialParam{graphics.CreateUniform(name, bgfx::UniformType::Vec4),
                         value};
  };
  material.params = {
      param("u_meshColor", data.base_color),
      param("u_materialParams",
            glm::vec4(data.metallic, data.roughness, 0.0f, 0.0f)),
      param("u_emissive",
            glm::vec4(data.emissive * data.emissive_strength, 0.0f)),
  };

  // Stages must match the samplers of fs_lit.sc. The texture type picks the
  // color space: color textures are sRGB, data textures linear
  const auto texture = [&](const char* sampler, uint8_t stage,
                           const std::string& key, TextureType type,
                           bgfx::TextureHandle fallback) {
    return MaterialTexture{
        graphics.CreateUniform(sampler, bgfx::UniformType::Sampler), stage,
        registry.Acquire(key, type), fallback};
  };
  const bgfx::TextureHandle white = graphics.GetWhiteTexture();
  material.textures = {
      texture("s_albedo", 0, data.albedo_map, TextureType::ALBEDO, white),
      texture("s_normalMap", 1, data.normal_map, TextureType::NORMAL,
              graphics.GetFlatNormalTexture()),
      texture("s_metallicRoughnessMap", 2, data.metallic_roughness_map,
              TextureType::ROUGHNESS, white),
      texture("s_occlusionMap", 3, data.ao_map, TextureType::OCCLUSION, white),
      texture("s_emissiveMap", 4, data.emissive_map, TextureType::EMISSIVE,
              white),
  };

  // TODO: normal scale, occlusion strength, KHR_texture_transform, UV sets,
  // alpha mode and cutoff, double-sided
  return material;
}
