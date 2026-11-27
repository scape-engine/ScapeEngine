#ifndef GENERATOR_PROPERTIES_H
#define GENERATOR_PROPERTIES_H

#include <string>

#include "engine/ecs/components/transform_component.h"
#include "engine/ecs/components/volume_component.h"
#include "engine/pcg/generator_base.h"

// Settings of the generators a volume can use, drawn in the Inspector
namespace GeneratorProperties {

// A generator's settings; a graph generator's graph asset edits in the PCG
// Graph tab
void Draw(ResourceID id);

// One stage of the generator (its name in the settings), open, as in Draw
void DrawStage(ResourceID id, const std::string& stage);

// Gives the volume a new generator of the type, named after it
void Create(VolumeComponent& volume, PCG::GeneratorType type);

// A child of the PCG world: a volume with a new generator of the type
Entity AddGenerator(Entity world, PCG::GeneratorType type);

// The graph generators using the graph asset (project-relative) read it
// again, after it was saved
void ReloadGraph(const std::string& graph);

}  // namespace GeneratorProperties

#endif  // GENERATOR_PROPERTIES_H
