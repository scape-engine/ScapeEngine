// Checks must run in release builds too
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <iostream>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/animation/skeleton.h"
#include "engine/geometry/ground/ground_mesh.h"
#include "engine/content/loaders/mesh_loader.h"

namespace Tests {

// ozz keeps keys compressed (16-bit rotations, half-float translations)
static bool Near(const glm::vec3& a, const glm::vec3& b) {
  return glm::length(a - b) < 1e-3f;
}

// A child one unit along x of a root that turns a quarter about y over a
// second: sampled between keys, looped and held, and blended half and half
// with a clip that holds the bind pose
static void TestSamplePose() {
  SkeletalData data;
  data.skeleton.names = {"root", "child"};
  data.skeleton.parents = {-1, 0};
  data.skeleton.locals = {glm::mat4(1.0f),
                          glm::translate(glm::mat4(1.0f), glm::vec3(1, 0, 0))};
  AnimationClip clip;
  clip.duration = 1.0f;
  AnimationClip::Channel turn;
  turn.node = 0;
  turn.rotation_times = {0.0f, 1.0f};
  turn.rotations = {glm::quat(1, 0, 0, 0),
                    glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0))};
  clip.channels.push_back(turn);
  AnimationClip still;
  still.duration = 1.0f;
  data.clips = {clip, still};
  assert(BuildRuntime(data) && data.runtime_clips.size() == 2);

  std::vector<glm::mat4> pose;
  const auto child = [&] { return glm::vec3(pose[1][3]); };
  const glm::vec3 half(std::cos(glm::radians(45.0f)), 0.0f,
                       -std::sin(glm::radians(45.0f)));
  SamplePose(data, 0, 0.5f, true, pose);
  assert(pose.size() == 2 && Near(child(), half));
  SamplePose(data, 0, 1.5f, true, pose);
  assert(Near(child(), half));
  SamplePose(data, 0, 2.0f, false, pose);
  assert(Near(child(), glm::vec3(0, 0, -1)));
  SamplePose(data, -1, 0.5f, true, pose);
  assert(Near(child(), glm::vec3(1, 0, 0)));

  PoseSampler sampler;
  sampler.Sample(data, {{0, 1.0f, 0.5f}, {1, 0.0f, 0.5f}}, pose);
  assert(Near(child(), half));
}

// A slope y = x / 2 as two triangles: the ground under points on it, above
// and below it, none past its edge
static void TestGroundMesh() {
  const GroundMesh ground({{-2, -1, -2}, {2, 1, -2}, {2, 1, 2},
                           {-2, -1, -2}, {2, 1, 2}, {-2, -1, 2}},
                          glm::vec3(0, 1, 0));
  glm::vec3 hit;
  assert(ground.Under(glm::vec3(1, 5, 0.5f), hit) &&
         Near(hit, glm::vec3(1, 0.5f, 0.5f)));
  assert(ground.Under(glm::vec3(-1, -3, 0), hit) &&
         Near(hit, glm::vec3(-1, -0.5f, 0)));
  assert(!ground.Under(glm::vec3(3, 0, 0), hit));
}

// A leg under a root: hip, a knee bent forward and the foot on the ground.
// Found among the bones, then planted: the foot lifted, and the body dropped
// with the foot kept where it stood
static void TestPlantLeg() {
  SkeletalData data;
  data.skeleton.names = {"root", "hip", "knee", "foot"};
  data.skeleton.parents = {-1, 0, 1, 2};
  data.skeleton.locals = {
      glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0)), glm::mat4(1.0f),
      glm::translate(glm::mat4(1.0f), glm::vec3(0.1f, -0.5f, 0)),
      glm::translate(glm::mat4(1.0f), glm::vec3(-0.1f, -0.5f, 0))};
  assert(BuildRuntime(data));
  FindLegs(data, {1, 2, 3});
  assert(data.legs.size() == 1 && data.legs[0].start == 1 &&
         data.legs[0].mid == 2 && data.legs[0].end == 3);

  std::vector<glm::mat4> pose;
  PoseSampler sampler;
  const auto at = [&](int node) { return glm::vec3(pose[node][3]); };
  sampler.Sample(data, {}, pose);
  sampler.Plant(data, glm::vec3(0.0f), {glm::vec3(0, 0.2f, 0)}, pose);
  assert(Near(at(3), glm::vec3(0, 0.2f, 0)) && Near(at(1), glm::vec3(0, 1, 0)));
  sampler.Sample(data, {}, pose);
  sampler.Plant(data, glm::vec3(0, -0.1f, 0), {glm::vec3(0, 0.1f, 0)}, pose);
  assert(Near(at(3), glm::vec3(0)) && Near(at(1), glm::vec3(0, 0.9f, 0)));
}

// An animated glb on the plain model path keeps its skeleton and clips, and
// every mesh comes skinned to it
static void TestAnimatedModel() {
  SkeletalData skeletal;
  const auto meshes = Content::MeshLoader::Load(
      "examples/example-project/models/creature.glb", &skeletal);
  assert(!meshes.empty() && !skeletal.Empty());
  assert(skeletal.clips.size() == 2 && skeletal.clips[0].duration > 0.0f);
  assert(skeletal.runtime_skeleton && skeletal.runtime_clips.size() == 2 &&
         skeletal.runtime_clips[0]);
  // The trot swings legs 0.8 m long 20 degrees either way in 0.8 s: about
  // 0.68 m/s ahead (+x), nothing sideways
  const int walk = skeletal.clips[0].name == "walk" ? 0 : 1;
  const float stride = StrideSpeed(skeletal, walk, glm::vec3(1, 0, 0));
  assert(stride > 0.6f && stride < 0.76f);
  assert(StrideSpeed(skeletal, walk, glm::vec3(0, 0, 1)) < 0.05f);
  for (const auto& mesh : meshes) {
    assert(!mesh.bone_nodes.empty());
    assert(mesh.joints.size() == mesh.vertices.size());
    for (int node : mesh.bone_nodes)
      assert(node >= 0 &&
             node < static_cast<int>(skeletal.skeleton.names.size()));
  }
  // Without the caller taking a skeleton, the old flat load
  assert(Content::MeshLoader::Load(
             "examples/example-project/models/creature.glb")
             .front()
             .bone_nodes.empty());
}

void TestAnimation() {
  TestSamplePose();
  TestGroundMesh();
  TestPlantLeg();
  TestAnimatedModel();
  std::cout << "Animation: OK" << std::endl;
}

}  // namespace Tests
