#include "mesh_loader.h"

#include <assimp/DefaultIOSystem.h>
#include <assimp/config.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>

#include "engine/content/loaders/material_loader.h"
#include "engine/core/logger.h"

namespace Content {

namespace {

glm::mat4 ToGlm(const aiMatrix4x4& m) {
  // Assimp's row-major 4x4 to glm column-major
  return glm::mat4(m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2, m.a3, m.b3,
                   m.c3, m.d3, m.a4, m.b4, m.c4, m.d4);
}

// The mesh's bones as skeleton nodes, and per vertex the four heaviest
// (aiProcess_LimitBoneWeights keeps at most four)
void ConvertBones(const aiMesh* ai_mesh, const Skeleton& skeleton,
                  Geometry::MeshData& out) {
  if (!ai_mesh->HasBones())
    return;
  out.joints.assign(ai_mesh->mNumVertices, glm::u8vec4(0));
  out.weights.assign(ai_mesh->mNumVertices, glm::vec4(0.0f));
  std::vector<uint8_t> used(ai_mesh->mNumVertices, 0);
  for (unsigned int b = 0; b < ai_mesh->mNumBones; ++b) {
    const aiBone* bone = ai_mesh->mBones[b];
    out.bone_nodes.push_back(skeleton.Find(bone->mName.C_Str()));
    out.bone_offsets.push_back(ToGlm(bone->mOffsetMatrix));
    for (unsigned int w = 0; w < bone->mNumWeights; ++w) {
      const unsigned int v = bone->mWeights[w].mVertexId;
      if (v >= used.size() || used[v] >= 4)
        continue;
      out.joints[v][used[v]] = static_cast<uint8_t>(b);
      out.weights[v][used[v]] = bone->mWeights[w].mWeight;
      ++used[v];
    }
  }
}

Geometry::MeshData ConvertMesh(const aiMesh* ai_mesh,
                               const Skeleton* skeleton = nullptr) {
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

  if (skeleton)
    ConvertBones(ai_mesh, *skeleton, out);
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
                               const Skeleton& skeleton,
                               std::vector<Geometry::MeshData>& out_meshes) {
  SceneNode result;
  result.name = node->mName.C_Str();

  result.local_transform = ToGlm(node->mTransformation);

  for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
    const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
    result.mesh_indices.push_back(static_cast<int>(out_meshes.size()));
    out_meshes.push_back(ConvertMesh(mesh, &skeleton));
  }

  for (unsigned int i = 0; i < node->mNumChildren; ++i) {
    result.children.push_back(ProcessNodeHierarchy(node->mChildren[i], scene,
                                                   skeleton, out_meshes));
  }

  return result;
}

void AddNodes(const aiNode* node, int parent, Skeleton& skeleton) {
  const int index = static_cast<int>(skeleton.names.size());
  skeleton.names.push_back(node->mName.C_Str());
  skeleton.parents.push_back(parent);
  skeleton.locals.push_back(ToGlm(node->mTransformation));
  for (unsigned int i = 0; i < node->mNumChildren; ++i)
    AddNodes(node->mChildren[i], index, skeleton);
}

bool HasBones(const aiScene* scene) {
  for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
    if (scene->mMeshes[i]->HasBones())
      return true;
  return false;
}

// An animated model's meshes, flat, all in model space and all skinned: a
// mesh without bones follows its own node rigidly. `index` counts nodes in
// the order AddNodes adds them
void ProcessAnimatedNode(const aiNode* node, const glm::mat4& parent,
                         const aiScene* scene, const Skeleton& skeleton,
                         int& index,
                         std::vector<Geometry::MeshData>& out_meshes) {
  const int node_index = index++;
  const glm::mat4 global = parent * ToGlm(node->mTransformation);
  const glm::mat3 normal_matrix =
      glm::transpose(glm::inverse(glm::mat3(global)));
  for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
    Geometry::MeshData mesh =
        ConvertMesh(scene->mMeshes[node->mMeshes[i]], &skeleton);
    if (mesh.bone_nodes.empty()) {
      for (auto& v : mesh.vertices) {
        v.position = glm::vec3(global * glm::vec4(v.position, 1.0f));
        v.normal = glm::normalize(normal_matrix * v.normal);
        v.tangent = glm::mat3(global) * v.tangent;
        v.bitangent = glm::mat3(global) * v.bitangent;
      }
      mesh.joints.assign(mesh.vertices.size(), glm::u8vec4(0));
      mesh.weights.assign(mesh.vertices.size(), glm::vec4(1, 0, 0, 0));
      mesh.bone_nodes = {node_index};
      mesh.bone_offsets = {glm::inverse(global)};
    }
    out_meshes.push_back(std::move(mesh));
  }
  for (unsigned int i = 0; i < node->mNumChildren; ++i)
    ProcessAnimatedNode(node->mChildren[i], global, scene, skeleton, index,
                        out_meshes);
}

AnimationClip ConvertAnimation(const aiAnimation* animation,
                               const Skeleton& skeleton) {
  const double ticks =
      animation->mTicksPerSecond > 0.0 ? animation->mTicksPerSecond : 1.0;
  AnimationClip clip;
  clip.name = animation->mName.C_Str();
  clip.duration = static_cast<float>(animation->mDuration / ticks);
  for (unsigned int c = 0; c < animation->mNumChannels; ++c) {
    const aiNodeAnim* in = animation->mChannels[c];
    AnimationClip::Channel channel;
    channel.node = skeleton.Find(in->mNodeName.C_Str());
    for (unsigned int k = 0; k < in->mNumPositionKeys; ++k) {
      const aiVectorKey& key = in->mPositionKeys[k];
      channel.position_times.push_back(static_cast<float>(key.mTime / ticks));
      channel.positions.emplace_back(key.mValue.x, key.mValue.y, key.mValue.z);
    }
    for (unsigned int k = 0; k < in->mNumRotationKeys; ++k) {
      const aiQuatKey& key = in->mRotationKeys[k];
      channel.rotation_times.push_back(static_cast<float>(key.mTime / ticks));
      channel.rotations.emplace_back(key.mValue.w, key.mValue.x, key.mValue.y,
                                     key.mValue.z);
    }
    for (unsigned int k = 0; k < in->mNumScalingKeys; ++k) {
      const aiVectorKey& key = in->mScalingKeys[k];
      channel.scale_times.push_back(static_cast<float>(key.mTime / ticks));
      channel.scales.emplace_back(key.mValue.x, key.mValue.y, key.mValue.z);
    }
    if (channel.node >= 0)
      clip.channels.push_back(std::move(channel));
  }
  return clip;
}
}  // namespace

// aiProcess_FlipUVs: Assimp returns texture coordinates with a bottom left
// origin, while textures are sampled with a top left one (the first stored
// row at v = 0) on every bgfx backend. aiProcess_OptimizeGraph and
// aiProcess_PreTransformVertices are left out to keep the hierarchy
static constexpr unsigned int scene_flags =
    aiProcess_Triangulate | aiProcess_GenSmoothNormals |
    aiProcess_JoinIdenticalVertices | aiProcess_CalcTangentSpace |
    aiProcess_LimitBoneWeights | aiProcess_SplitByBoneCount |
    aiProcess_FlipUVs;

std::vector<Geometry::MeshData> MeshLoader::Load(const std::string& path,
                                                 SkeletalData* skeletal) {
  Assimp::Importer importer;

  // PreTransformVertices joins meshes that share a material by default;
  // keeping the hierarchy bakes transforms but keeps every part separate
  importer.SetPropertyBool(AI_CONFIG_PP_PTV_KEEP_HIERARCHY, true);
  // Meshes with more bones than a draw takes (Mesh::max_bones) split
  importer.SetPropertyInteger(AI_CONFIG_PP_SBBC_MAX_BONES, 64);

  std::string base_dir = path.substr(0, path.find_last_of("/\\") + 1);
  Logger::getInstance().Log(LogLevel::Debug,
                            "[MeshLoader] Loading from base_dir: " + base_dir);

  // Read first, then post-process by kind: an animated model (skins or
  // clips) keeps its hierarchy when the caller takes its skeleton; the rest
  // bake node transforms (e.g. a model's up-axis rotation) into the vertices,
  // as the flat mesh list has no nodes
  const aiScene* scene = importer.ReadFile(path, 0);
  const bool animated = skeletal && scene &&
                        (scene->mNumAnimations > 0 || HasBones(scene));
  if (scene)
    scene = importer.ApplyPostProcessing(
        animated ? scene_flags
                 : aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                       aiProcess_JoinIdenticalVertices |
                       aiProcess_ImproveCacheLocality |
                       aiProcess_CalcTangentSpace |
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
  if (animated) {
    AddNodes(scene->mRootNode, -1, skeletal->skeleton);
    int index = 0;
    ProcessAnimatedNode(scene->mRootNode, glm::mat4(1.0f), scene,
                        skeletal->skeleton, index, mesh_data);
    for (unsigned int i = 0; i < scene->mNumAnimations; ++i)
      skeletal->clips.push_back(
          ConvertAnimation(scene->mAnimations[i], skeletal->skeleton));
    if (!BuildRuntime(*skeletal))
      Logger::getInstance().Log(LogLevel::Warning,
                                "[MeshLoader] Too many nodes to animate: " +
                                    path);
  } else {
    ProcessNode(scene->mRootNode, scene, mesh_data);
  }

  Logger::getInstance().Log(LogLevel::Debug,
                            "[MeshLoader] Loaded " +
                                std::to_string(mesh_data.size()) +
                                " mesh_data from " + path);

  return mesh_data;
}

SceneData MeshLoader::LoadScene(const std::string& path) {
  Assimp::Importer importer;
  // Meshes with more bones than a draw takes (Mesh::max_bones) split
  importer.SetPropertyInteger(AI_CONFIG_PP_SBBC_MAX_BONES, 64);

  std::string base_dir = path.substr(0, path.find_last_of("/\\") + 1);
  Logger::getInstance().Log(LogLevel::Debug,
                            "[MeshLoader] Loading from base_dir: " + base_dir);

  const aiScene* scene = importer.ReadFile(path, scene_flags);

  if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) ||
      !scene->mRootNode) {
    Logger::getInstance().Log(
        LogLevel::Error, "[MeshLoader] Failed to load scene: " + path + " - " +
                             importer.GetErrorString());
    return {};
  }

  SceneData data;

  AddNodes(scene->mRootNode, -1, data.skeletal.skeleton);
  data.root = ProcessNodeHierarchy(scene->mRootNode, scene,
                                   data.skeletal.skeleton, data.mesh_data);
  for (unsigned int i = 0; i < scene->mNumAnimations; ++i)
    data.skeletal.clips.push_back(
        ConvertAnimation(scene->mAnimations[i], data.skeletal.skeleton));
  if ((!data.skeletal.clips.empty() || HasBones(scene)) &&
      !BuildRuntime(data.skeletal))
    Logger::getInstance().Log(
        LogLevel::Warning, "[MeshLoader] Too many nodes to animate: " + path);
  // The meshes' material indices index these
  data.materials = MaterialLoader::LoadFromScene(path);

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
