#ifndef FADE_SYSTEM_H
#define FADE_SYSTEM_H

#include "engine/ecs/world.h"

// Steps FadeComponents and answers how visible an entity is
namespace FadeSystem {

// Moves each FadeComponent's visibility toward its target by `delta` seconds,
// and destroys the faded-out ones that ask for it
void Update(ECS& ecs, float delta);

// The entity's visibility: its FadeComponent's times its parents' (1 without
// any)
float Visibility(ECS& ecs, Entity entity);

}  // namespace FadeSystem

#endif  // FADE_SYSTEM_H
