#include "building_library.h"

#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

namespace ProcWorld {

static glm::mat4 _ToGlm(const aiMatrix4x4& m) {
  return glm::mat4(m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2, m.a3, m.b3,
                   m.c3, m.d3, m.a4, m.b4, m.c4, m.d4);
}

Worldgen::SceneTransform SceneTransformOf(const glm::mat4& local) {
  Worldgen::SceneTransform t;
  glm::mat4 rotation(1.0f);
  for (int axis = 0; axis < 3; ++axis) {
    t.scale[axis] = glm::length(glm::vec3(local[axis]));
    if (t.scale[axis] > 0.0f)
      rotation[axis] = glm::vec4(glm::vec3(local[axis]) / t.scale[axis], 0.0f);
    t.translation[axis] = local[3][axis];
  }
  // SceneMatrix turns about z, then x, then y
  float y, x, z;
  glm::extractEulerAngleYXZ(rotation, y, x, z);
  t.rotation[0] = glm::degrees(x);
  t.rotation[1] = glm::degrees(y);
  t.rotation[2] = glm::degrees(z);
  return t;
}

static Worldgen::SceneNode _Convert(const aiNode* node, const aiScene* scene) {
  Worldgen::SceneNode out;
  out.name = node->mName.C_Str();
  out.transform = SceneTransformOf(_ToGlm(node->mTransformation));
  if (node->mNumMeshes > 0) {
    out.kind = Worldgen::SceneNodeKind::Mesh;
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
      const aiAABB& box = scene->mMeshes[node->mMeshes[i]]->mAABB;
      lo = glm::min(lo, glm::vec3(box.mMin.x, box.mMin.y, box.mMin.z));
      hi = glm::max(hi, glm::vec3(box.mMax.x, box.mMax.y, box.mMax.z));
    }
    using A = Worldgen::SceneAttribute;
    const A keys[6] = {A::BoundsMinX, A::BoundsMinY, A::BoundsMinZ,
                       A::BoundsMaxX, A::BoundsMaxY, A::BoundsMaxZ};
    for (int k = 0; k < 6; ++k)
      out.attributes.push_back(
          {keys[k], std::to_string(k < 3 ? lo[k] : hi[k - 3])});
  } else {
    out.kind = Worldgen::SceneNodeKind::Locator;
  }
  for (unsigned int i = 0; i < node->mNumChildren; ++i)
    out.children.push_back(_Convert(node->mChildren[i], scene));
  return out;
}

std::unique_ptr<Worldgen::SceneNode> LoadGlbScene(
    const std::filesystem::path& path) {
  Assimp::Importer importer;
  const aiScene* scene =
      importer.ReadFile(path.string(), aiProcess_GenBoundingBoxes);
  if (!scene || !scene->mRootNode)
    return nullptr;
  auto out = std::make_unique<Worldgen::SceneNode>(
      _Convert(scene->mRootNode, scene));
  out->transform = Worldgen::SceneTransform();  // the scene's own frame
  return out;
}

BuildingLibrary::BuildingLibrary(std::vector<Worldgen::BuildingStyle> styles,
                                 std::filesystem::path root)
    : styles_(std::move(styles)), root_(std::move(root)) {
  // Templates and locator names come with the styles
  files_.scene = [this](const std::string& file) { return Scene(file); };
  files_.lsystem = [](const std::string&) {
    return static_cast<const Worldgen::LSystem::File*>(nullptr);
  };
}

const Worldgen::SceneNode* BuildingLibrary::Scene(const std::string& file) {
  std::lock_guard lock(scenes_mutex_);
  auto it = scenes_.find(file);
  if (it == scenes_.end())
    it = scenes_.emplace(file, LoadGlbScene(root_ / file)).first;
  return it->second.get();
}

std::shared_ptr<const std::vector<Worldgen::BuildingPiece>>
BuildingLibrary::Structure(int cls, uint64_t seed) {
  {
    std::lock_guard lock(structures_mutex_);
    if (auto it = structures_.find({cls, seed}); it != structures_.end())
      return it->second;
  }
  // Two jobs may build the same one; the first kept wins
  auto pieces = std::make_shared<const std::vector<Worldgen::BuildingPiece>>(
      cls >= 0 && cls < static_cast<int>(styles_.size())
          ? Worldgen::BuildStructure(styles_[cls], seed, files_)
          : std::vector<Worldgen::BuildingPiece>());
  std::lock_guard lock(structures_mutex_);
  // Instanced buildings keep no hold on their pieces, so a full cache can
  // simply start over
  if (structures_.size() >= max_structures)
    structures_.clear();
  return structures_.emplace(std::pair{cls, seed}, pieces).first->second;
}

}  // namespace ProcWorld
