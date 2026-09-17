#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/ecs/ecs_collection.h"
#include "engine/transform/transform.h"
#include "engine/transform/transform_system.h"

class TransformHierarchy : public ::testing::Test {
protected:
  void SetUp() override { entt::locator<ECS>::emplace(); }
  void TearDown() override { entt::locator<ECS>::reset(); }
};

TEST(Transform, EvaluateLocalMatrixProducesFiniteData) {
  TransformComponent t;
  t.position_ = {1.0f, 2.0f, 3.0f};
  t.rotation_ =
      glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
  t.scale_ = {2.0f, 2.0f, 2.0f};

  Transform::Evaluate(t);

  auto finite = [](const auto& v) {
    return !glm::any(glm::isnan(v)) && !glm::any(glm::isinf(v));
  };

  for (int i = 0; i < 4; ++i)
    EXPECT_TRUE(finite(t.model_[i])) << "model_ column " << i;
  for (int i = 0; i < 3; ++i)
    EXPECT_TRUE(finite(t.normal_[i])) << "normal_ column " << i;
}

TEST(Transform, SetPositionAndGetPositionLocalSpace) {
  TransformComponent t;
  t.position_ = {0.0f, 0.0f, 0.0f};

  Transform::SetPosition(t, {4.0f, 5.0f, 6.0f}, Space::LOCAL);

  EXPECT_EQ(Transform::GetPosition(t, Space::LOCAL),
            glm::vec3(4.0f, 5.0f, 6.0f));
}

TEST(Transform, RotationProducesExpectedForwardDirection) {
  TransformComponent t;
  t.rotation_ =
      glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

  glm::vec3 forward = Transform::Forward(t, Space::LOCAL);

  EXPECT_NEAR(forward.x, 1.0f, 1e-4f);
  EXPECT_NEAR(forward.y, 0.0f, 1e-4f);
  EXPECT_NEAR(forward.z, 0.0f, 1e-4f);
}

TEST_F(TransformHierarchy, SetWorldRotationWithRotatedGrandparent) {
  ECS& ecs = ECS::Main();
  auto [gpId, gp] = ecs.CreateEntity("gp");
  auto [pId, p] = ecs.CreateEntity("p", gpId);
  auto [cId, c] = ecs.CreateEntity("c", pId);

  gp.rotation_ = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
  Transform::Evaluate(gp);
  Transform::Evaluate(p, gp);

  Transform::SetRotation(c, glm::quat(1, 0, 0, 0), Space::WORLD);
  Transform::Evaluate(c, p);

  glm::quat world = Transform::GetRotation(c, Space::WORLD);
  EXPECT_NEAR(glm::abs(glm::dot(world, glm::quat(1, 0, 0, 0))), 1.0f, 1e-5f);
}

TEST_F(TransformHierarchy, TransformSystemPropagatesParentChange) {
  ECS& ecs = ECS::Main();
  auto [parentId, parent] = ecs.CreateEntity("parent");
  auto [childId, child] = ecs.CreateEntity("child", parentId);

  parent.position_ = {10.0f, 0.0f, 0.0f};
  child.position_ = {2.0f, 0.0f, 0.0f};

  TransformSystem system;
  system.Perform(glm::mat4(1.0f));
  EXPECT_NEAR(child.model_[3].x, 12.0f, 1e-4f);

  // Only the parent is marked dirty; the child must still update.
  Transform::SetPosition(parent, {20.0f, 0.0f, 0.0f}, Space::LOCAL);
  system.Perform(glm::mat4(1.0f));

  EXPECT_NEAR(child.model_[3].x, 22.0f, 1e-4f);
  EXPECT_FALSE(child.modified_);
  EXPECT_FALSE(parent.modified_);
}

TEST(Transform, EulerAndQuatRoundTripIsStable) {
  glm::vec3 euler = {30.0f, 45.0f, 60.0f};
  glm::quat q = Transform::ToQuat(euler);
  glm::quat roundTrip = Transform::ToQuat(Transform::ToEuler(q));

  EXPECT_NEAR(glm::abs(glm::dot(q, roundTrip)), 1.0f, 1e-5f);
}

TEST(Transform, EulerRoundTripNearGimbalLock) {
  glm::quat q = Transform::ToQuat({20.0f, 90.0f, 10.0f});
  glm::quat roundTrip = Transform::ToQuat(Transform::ToEuler(q));

  EXPECT_NEAR(glm::abs(glm::dot(q, roundTrip)), 1.0f, 1e-5f);
}