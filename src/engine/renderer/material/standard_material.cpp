#include "standard_material.h"

#include "engine/context/engine_context.h"
#include "engine/renderer/graphics_renderer.h"

std::vector<std::pair<std::string, TextureType>> StandardMaterial::Textures(
    const Content::MaterialData& data) {
  // Order matches the samplers in Build (stages 0-4)
  return {{data.albedo_map, TextureType::ALBEDO},
          {data.normal_map, TextureType::NORMAL},
          {data.metallic_roughness_map, TextureType::ROUGHNESS},
          {data.ao_map, TextureType::OCCLUSION},
          {data.emissive_map, TextureType::EMISSIVE}};
}

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
  // Alpha cutoff 0 never discards (opaque); the depth passes (pre-pass,
  // shadows) read it too, so masked holes stay holes there
  const float alpha_cutoff =
      data.alpha_mode == Content::AlphaMode::Mask ? data.alpha_cutoff : 0.0f;
  material.params = {
      param("u_meshColor", data.base_color),
      param("u_materialParams",
            glm::vec4(data.metallic, data.roughness, alpha_cutoff,
                      data.double_sided ? 1.0f : 0.0f)),
      param("u_emissive",
            glm::vec4(data.emissive * data.emissive_strength, 0.0f)),
  };

  // Stages must match the samplers of fs_lit.sc. The texture type picks the
  // color space: color textures are sRGB, data textures linear
  const auto textures = Textures(data);
  const auto texture = [&](const char* sampler, uint8_t stage,
                           bgfx::TextureHandle fallback) {
    const auto& [key, type] = textures[stage];
    return MaterialTexture{
        graphics.CreateUniform(sampler, bgfx::UniformType::Sampler), stage,
        registry.Acquire(key, type), fallback};
  };
  const bgfx::TextureHandle white = graphics.GetWhiteTexture();
  material.textures = {
      texture("s_albedo", 0, white),
      texture("s_normalMap", 1, graphics.GetFlatNormalTexture()),
      texture("s_metallicRoughnessMap", 2, white),
      texture("s_occlusionMap", 3, white),
      texture("s_emissiveMap", 4, white),
  };

  material.double_sided = data.double_sided;

  // TODO: normal scale, occlusion strength, KHR_texture_transform, UV sets,
  // alpha blending
  return material;
}
