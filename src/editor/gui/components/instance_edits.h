#ifndef INSTANCE_EDITS_H
#define INSTANCE_EDITS_H

#include <cstdint>

#include "engine/ecs/ecs_collection.h"
#include "engine/pcg/generator_resource.h"

// Hand edits on what generators placed, from the Inspector: a picked
// instance is removed, moved, made another object or given another seed, and
// a generator's edit layer is shown and cleared. Each change hands the
// generator a new edit layer (PCG::EditLayer); the generator remakes what it
// changes
namespace InstanceEdits {

// Whether the instance (of the entity's InstancedMeshComponent) takes edits
// TODO: edits on objects that are entities of their own (animated models,
// primitives), on buildings and herds, and on a region generator's objects;
// only a planet's instanced Objects stage objects take them
bool Editable(Entity entity, int instance);

// The instance's fields, as rows of the Inspector's Instance section, marked
// where they override what its generator placed (right-click: Revert)
void Draw(Entity entity, int instance);

// Under its name, as Unity's prefab row: an Overrides dropdown listing what
// overrides the generator (Revert All), and Remove
void DrawOverrides(Entity entity, int instance);

// The instance takes another seed
void SetSeed(Entity entity, int instance, uint64_t seed);

// The generator's edit layer: its file, how many edits, those whose
// instances its rules no longer place (`orphans`), and clearing it
void DrawLayer(PCG::GeneratorResource& resource, size_t orphans);

// Once per frame: an edited instance, made again, is selected again
void Update();

}  // namespace InstanceEdits

#endif  // INSTANCE_EDITS_H
