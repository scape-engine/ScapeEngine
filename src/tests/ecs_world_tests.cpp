#include <gtest/gtest.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

struct Health {
  int value = 100;
};

struct Position {
  glm::vec3 v{0.0f};
};

struct Velocity {
  glm::vec3 v{0.0f};
};

struct Tag {
  const char* name = "";
};

TEST(ECS, CreateEntityAndAttachComponent) {
  entt::registry registry;

  const auto entity = registry.create();
  registry.emplace<Health>(entity, 80);
  registry.emplace<Position>(entity, glm::vec3(1.0f, 2.0f, 3.0f));

  EXPECT_TRUE(registry.valid(entity));
  EXPECT_TRUE((registry.all_of<Health, Position>(entity)));
  EXPECT_EQ(registry.get<Health>(entity).value, 80);
  EXPECT_EQ(registry.get<Position>(entity).v, glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST(ECS, RemoveComponentDoesNotDestroyEntity) {
  entt::registry registry;

  const auto e = registry.create();
  registry.emplace<Health>(e, 42);

  EXPECT_TRUE((registry.all_of<Health>(e)));

  registry.remove<Health>(e);

  EXPECT_FALSE((registry.all_of<Health>(e)));
  EXPECT_TRUE(registry.valid(e));
}

TEST(ECS, QueryOnlyReturnsMatchingEntities) {
  entt::registry registry;

  const auto a = registry.create();
  const auto b = registry.create();
  const auto c = registry.create();

  registry.emplace<Position>(a, glm::vec3(0.0f));
  registry.emplace<Position>(b, glm::vec3(5.0f));
  registry.emplace<Velocity>(b, glm::vec3(1.0f));
  registry.emplace<Position>(c, glm::vec3(10.0f));

  auto view = registry.view<Position, Velocity>();

  int count = 0;
  view.each([&](const entt::entity entity, Position& pos, Velocity& vel) {
    ++count;
    EXPECT_EQ(entity, b);
    EXPECT_EQ(pos.v, glm::vec3(5.0f));
    EXPECT_EQ(vel.v, glm::vec3(1.0f));
  });

  EXPECT_EQ(count, 1);
}

TEST(ECS, RepeatedAddAndRemoveComponentIsStable) {
  entt::registry registry;

  const auto e = registry.create();

  registry.emplace<Health>(e, 10);
  registry.remove<Health>(e);

  registry.emplace<Health>(e, 25);

  EXPECT_TRUE((registry.all_of<Health>(e)));
  EXPECT_EQ(registry.get<Health>(e).value, 25);
}

TEST(ECS, HierarchyTransformPropagation) {
  entt::registry registry;

  const auto parent = registry.create();
  const auto child = registry.create();

  registry.emplace<Position>(parent, glm::vec3(10.0f, 0.0f, 0.0f));
  registry.emplace<Position>(child, glm::vec3(2.0f, 0.0f, 0.0f));

  registry.emplace<Tag>(parent, "root");
  registry.emplace<Tag>(child, "leaf");

  const auto world_pos =
      registry.get<Position>(parent).v + registry.get<Position>(child).v;

  EXPECT_EQ(world_pos, glm::vec3(12.0f, 0.0f, 0.0f));
}

TEST(ECS, EntityReuseAfterDestroyIsSafe) {
  entt::registry registry;

  const auto e = registry.create();
  registry.emplace<Health>(e, 100);

  registry.destroy(e);

  EXPECT_FALSE(registry.valid(e));

  const auto e2 = registry.create();
  registry.emplace<Health>(e2, 50);

  EXPECT_TRUE(registry.valid(e2));
  EXPECT_TRUE((registry.all_of<Health>(e2)));
  EXPECT_EQ(registry.get<Health>(e2).value, 50);
}