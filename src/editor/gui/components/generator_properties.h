#ifndef GENERATOR_PROPERTIES_H
#define GENERATOR_PROPERTIES_H

#include "engine/ecs/components/transform_component.h"
#include "engine/ecs/components/volume_component.h"
#include "engine/pcg/generator_base.h"

// Settings of the generators a volume can use, drawn in the Inspector
namespace GeneratorProperties {

// A planet, craft or region generator's settings; graphs edit in the PCG tab
void Draw(ResourceID id);

// Gives the volume a new generator of the type, named after it
void Create(VolumeComponent& volume, PCG::GeneratorType type);

// A child of the PCG world: a volume with a new generator of the type
Entity AddGenerator(Entity world, PCG::GeneratorType type);

}  // namespace GeneratorProperties

#endif  // GENERATOR_PROPERTIES_H
