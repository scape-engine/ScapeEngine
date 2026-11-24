// Checks must run in release builds too
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <iostream>

#include "engine/ecs/components/fade_component.h"
#include "engine/fade/fade_system.h"

namespace Tests {

static bool Near(float a, float b) { return std::abs(a - b) < 1e-5f; }

// Fading in and out over the duration, at once without one, a child taking
// its parent's visibility, and removal once faded out
void TestFade() {
  ECS ecs;
  auto [parent, parent_transform] = ecs.CreateEntity("Parent");
  auto [child, child_transform] = ecs.CreateEntity("Child", parent);
  assert(Near(FadeSystem::Visibility(ecs, child), 1.0f));

  auto& fade = ecs.Reg().emplace<FadeComponent>(parent);
  fade.visibility_ = 0.0f;
  fade.target_ = 1.0f;
  fade.duration_ = 2.0f;
  FadeSystem::Update(ecs, 0.5f);
  assert(Near(fade.visibility_, 0.25f));
  assert(Near(FadeSystem::Visibility(ecs, child), 0.25f));
  FadeSystem::Update(ecs, 10.0f);
  assert(Near(fade.visibility_, 1.0f));

  // Both fading multiply
  auto& own = ecs.Reg().emplace<FadeComponent>(child);
  own.visibility_ = own.target_ = 0.5f;
  fade.target_ = 0.5f;
  fade.duration_ = 0.0f;
  FadeSystem::Update(ecs, 0.01f);
  assert(Near(fade.visibility_, 0.5f));
  assert(Near(FadeSystem::Visibility(ecs, child), 0.25f));

  // Faded out and removed, with its child
  fade.target_ = 0.0f;
  fade.duration_ = 1.0f;
  fade.destroy_when_hidden_ = true;
  FadeSystem::Update(ecs, 0.25f);
  assert(ecs.Reg().valid(parent) && Near(fade.visibility_, 0.25f));
  FadeSystem::Update(ecs, 0.25f);
  assert(!ecs.Reg().valid(parent) && !ecs.Reg().valid(child));

  std::cout << "Fade: OK" << std::endl;
}

}  // namespace Tests
