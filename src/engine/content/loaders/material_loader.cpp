#include "material_loader.h"

#include <assimp/DefaultIOSystem.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>

#include <filesystem>

#include "engine/content/cache/image_cache.h"
#include "engine/content/loaders/image_loader.h"
#include "engine/core/logger.h"

namespace Content {

namespace {

// Image cache key of the first texture of the given types (in order), or
// empty if the material has none. Embedded images (e.g. in a .glb) are
// decoded now, while the scene that owns them is alive
std::string TextureKey(const aiScene* scene, const aiMaterial* material,
                       std::initializer_list<aiTextureType> types,
                       const std::string& model_path,
                       const std::filesystem::path& base_dir) {
  aiString reference;
  for (aiTextureType type : types) {
    if (material->GetTexture(type, 0, &reference) == AI_SUCCESS)
      break;
    reference.Clear();
  }
  if (reference.length == 0)
    return {};

  // Embedded: "*<index>" references, or the image name in some formats
  if (const aiTexture* embedded = scene->GetEmbeddedTexture(reference.C_Str())) {
    const std::string key = model_path + "#" + reference.C_Str();
    auto& cache = ImageCache::Instance();

    // Compressed (PNG, JPG, ...) when mHeight is 0, mWidth is the byte size
    if (!cache.Contains(key) && embedded->mHeight == 0) {
      cache.Put(key, ImageLoader::LoadMemory(embedded->pcData,
                                             embedded->mWidth));
    }

    // TODO: uncompressed embedded texels (mHeight > 0, BGRA aiTexel)
    return cache.Contains(key) ? key : std::string{};
  }

  // TODO: decode URL-encoded URIs (e.g. "%20")
  return (base_dir / reference.C_Str()).lexically_normal().string();
}

}  // namespace

std::vector<MaterialData> MaterialLoader::ExtractMaterials(
    const aiScene* scene, const std::string& model_path) {
  std::vector<MaterialData> materials;
  materials.reserve(scene->mNumMaterials);

  const std::filesystem::path base_dir =
      std::filesystem::path(model_path).parent_path();

  // Query Assimp's shading model
  for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
    const aiMaterial* ai_mat = scene->mMaterials[i];
    MaterialData mat;

    aiString name;
    if (ai_mat->Get(AI_MATKEY_NAME, name) == AI_SUCCESS)
      mat.name = name.C_Str();

    // Detect shading model to know which keys to query
    int shading_model = 0;
    ai_mat->Get(AI_MATKEY_SHADING_MODEL, shading_model);
    bool is_pbr = (shading_model == aiShadingMode_PBR_BRDF);

    // If shading model is unclear, check for PBR keys directly
    if (!is_pbr) {
      float metallic = 0.0f;
      is_pbr = (ai_mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS);
    }

    aiColor4D color;
    if (is_pbr) {
      if (ai_mat->Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS)
        mat.base_color = glm::vec4(color.r, color.g, color.b, color.a);
    } else {
      if (ai_mat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
        mat.base_color = glm::vec4(color.r, color.g, color.b, color.a);
    }

    // PBR factors
    if (is_pbr) {
      ai_mat->Get(AI_MATKEY_METALLIC_FACTOR, mat.metallic);
      ai_mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, mat.roughness);
    } else {
      // Map Phong specular to rough approximation
      float shininess = 0.0f;
      if (ai_mat->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS)
        mat.roughness = 1.0f - glm::clamp(shininess / 256.0f, 0.0f, 1.0f);
    }

    // Texture slots. Assimp's glTF 2.0 importer stores metallic-roughness in
    // aiTextureType_GLTF_METALLIC_ROUGHNESS and occlusion in
    // aiTextureType_LIGHTMAP; the other types cover other formats
    const auto key = [&](std::initializer_list<aiTextureType> types) {
      return TextureKey(scene, ai_mat, types, model_path, base_dir);
    };
    mat.albedo_map = key({aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE});
    mat.normal_map = key({aiTextureType_NORMALS});
    mat.metallic_roughness_map =
        key({aiTextureType_GLTF_METALLIC_ROUGHNESS, aiTextureType_UNKNOWN});
    mat.ao_map =
        key({aiTextureType_LIGHTMAP, aiTextureType_AMBIENT_OCCLUSION});
    mat.emissive_map = key({aiTextureType_EMISSIVE});

    aiColor3D emissive;
    if (ai_mat->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS)
      mat.emissive = glm::vec3(emissive.r, emissive.g, emissive.b);

    // ? store on MaterialData for shader selection later
    mat.is_pbr = is_pbr;
    materials.push_back(mat);
  }

  Logger::getInstance().Log(LogLevel::Debug,
                            "[MaterialLoader] Extracted " +
                                std::to_string(materials.size()) +
                                " materials from " + model_path);

  return materials;
}

std::vector<MaterialData> MaterialLoader::LoadFromScene(
    const std::string& path) {
  Assimp::Importer importer;

  // Extract base directory and set it as the working path
  std::string base_dir = path.substr(0, path.find_last_of("/\\") + 1);
  Logger::getInstance().Log(
      LogLevel::Debug, "[MaterialLoader] Loading from base_dir: " + base_dir);

  const aiScene* scene = importer.ReadFile(
      path, aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                aiProcess_JoinIdenticalVertices | aiProcess_CalcTangentSpace);

  if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) ||
      !scene->mRootNode) {
    Logger::getInstance().Log(LogLevel::Error,
                              "[MaterialLoader] Failed to load scene: " + path +
                                  " - " + importer.GetErrorString());
    return {};
  }

  return ExtractMaterials(scene, path);
}

}  // namespace Content