#include "mesh_loader.h"

#include <assimp/DefaultIOSystem.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>

#include "engine/core/logger.h"

namespace Content {

namespace {

Geometry::MeshData ConvertMesh(const aiMesh* ai_mesh) {
  Geometry::MeshData out;
  out.vertices.reserve(ai_mesh->mNumVertices);
  out.material_index = ai_mesh->mMaterialIndex;

  for (unsigned int i = 0; i < ai_mesh->mNumVertices; ++i) {
    Geometry::VertexData v;
    v.position = {ai_mesh->mVertices[i].x, ai_mesh->mVertices[i].y,
                  ai_mesh->mVertices[i].z};
    v.normal = ai_mesh->HasNormals()
                   ? glm::vec3(ai_mesh->mNormals[i].x, ai_mesh->mNormals[i].y,
                               ai_mesh->mNormals[i].z)
                   : glm::vec3(0.0f);
    v.uv = ai_mesh->HasTextureCoords(0)
               ? glm::vec2(ai_mesh->mTextureCoords[0][i].x,
                           ai_mesh->mTextureCoords[0][i].y)
               : glm::vec2(0.0f);
    v.tangent =
        ai_mesh->HasTangentsAndBitangents()
            ? glm::vec3(ai_mesh->mTangents[i].x, ai_mesh->mTangents[i].y,
                        ai_mesh->mTangents[i].z)
            : glm::vec3(0.0f);
    v.bitangent =
        ai_mesh->HasTangentsAndBitangents()
            ? glm::vec3(ai_mesh->mBitangents[i].x, ai_mesh->mBitangents[i].y,
                        ai_mesh->mBitangents[i].z)
            : glm::vec3(0.0f);
    out.vertices.push_back(v);
  }

  for (unsigned int f = 0; f < ai_mesh->mNumFaces; ++f) {
    const aiFace& face = ai_mesh->mFaces[f];
    for (unsigned int j = 0; j < face.mNumIndices; ++j)
      out.indices.push_back(face.mIndices[j]);
  }

  return out;
}

void ProcessNode(const aiNode* node, const aiScene* scene,
                 std::vector<Geometry::MeshData>& out_meshes) {
  for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
    const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
    out_meshes.push_back(ConvertMesh(mesh));
  }

  for (unsigned int i = 0; i < node->mNumChildren; ++i) {
    ProcessNode(node->mChildren[i], scene, out_meshes);
  }
}

// Build SceneNode tree
SceneNode ProcessNodeHierarchy(const aiNode* node, const aiScene* scene,
                               std::vector<Geometry::MeshData>& out_meshes) {
  SceneNode result;
  result.name = node->mName.C_Str();

  // Convert Assimp's row-major 4x4 to glm column-major
  const auto& m = node->mTransformation;
  result.local_transform =
      glm::mat4(m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2, m.a3, m.b3,
                m.c3, m.d3, m.a4, m.b4, m.c4, m.d4);

  for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
    const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
    result.mesh_indices.push_back(static_cast<int>(out_meshes.size()));
    out_meshes.push_back(ConvertMesh(mesh));
  }

  for (unsigned int i = 0; i < node->mNumChildren; ++i) {
    result.children.push_back(
        ProcessNodeHierarchy(node->mChildren[i], scene, out_meshes));
  }

  return result;
}
}  // namespace

std::vector<Geometry::MeshData> MeshLoader::Load(const std::string& path) {
  Assimp::Importer importer;

  std::string base_dir = path.substr(0, path.find_last_of("/\\") + 1);
  Logger::getInstance().Log(LogLevel::Debug,
                            "[MeshLoader] Loading from base_dir: " + base_dir);

  // The flat mesh list has no nodes, so node transforms (e.g. a model's
  // up-axis rotation) are baked into the vertices.
  // aiProcess_FlipUVs: Assimp returns texture coordinates with a bottom left
  // origin, while textures are sampled with a top left one (the first stored
  // row at v = 0) on every bgfx backend
  const aiScene* scene = importer.ReadFile(
      path, aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                aiProcess_JoinIdenticalVertices |
                aiProcess_ImproveCacheLocality | aiProcess_CalcTangentSpace |
                aiProcess_PreTransformVertices | aiProcess_FlipUVs);

  if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) ||
      !scene->mRootNode) {
    Logger::getInstance().Log(LogLevel::Error,
                              "[MeshLoader] Failed to load: " + path + " - " +
                                  importer.GetErrorString());

    // return interleaved vertices
    return {};
  }

  std::vector<Geometry::MeshData> mesh_data;
  ProcessNode(scene->mRootNode, scene, mesh_data);

  Logger::getInstance().Log(LogLevel::Debug, "[MeshLoader] Loaded " +
                                                 std::to_string(mesh_data.size()) +
                                                 " mesh_data from " + path);

  return mesh_data;
}

SceneData MeshLoader::LoadScene(const std::string& path) {
  Assimp::Importer importer;

  std::string base_dir = path.substr(0, path.find_last_of("/\\") + 1);
  Logger::getInstance().Log(LogLevel::Debug,
                            "[MeshLoader] Loading from base_dir: " + base_dir);

  // aiProcess_OptimizeGraph and aiProcess_PreTransformVertices omitted to
  // preserve hierarchy
  // aiProcess_FlipUVs: top left texture coordinate origin, see Load()
  const aiScene* scene = importer.ReadFile(
      path, aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                aiProcess_JoinIdenticalVertices | aiProcess_CalcTangentSpace |
                aiProcess_FlipUVs);

  if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) ||
      !scene->mRootNode) {
    Logger::getInstance().Log(
        LogLevel::Error, "[MeshLoader] Failed to load scene: " + path + " - " +
                             importer.GetErrorString());
    return {};
  }

  SceneData data;

  data.root = ProcessNodeHierarchy(scene->mRootNode, scene, data.mesh_data);

  Logger::getInstance().Log(LogLevel::Debug,
                            "[MeshLoader] Loaded scene with " +
                                std::to_string(data.mesh_data.size()) +
                                " mesh_data from " + path);

  return data;
}

bool MeshLoader::IsSupported(const std::string& path) {
  Assimp::Importer importer;

  return importer.IsExtensionSupported(path.substr(path.rfind('.')));
}

}  // namespace Content
