#ifndef ACTIVATION_H
#define ACTIVATION_H

#include "engine/ecs/world.h"

// Sets each ActivationComponent's active_ from the viewers (Viewers): once a
// frame, after Viewers::Update and before the systems that read it
namespace Activation {

void Update(ECS& ecs);

}  // namespace Activation

#endif  // ACTIVATION_H
