#ifndef PROCEDURAL_INSTANCE_COMPONENT_H
#define PROCEDURAL_INSTANCE_COMPONENT_H

#include <entt/entt.hpp>

using Entity = entt::entity;

// An entity a PCG system made from a source entity (e.g. a volume). It is
// remade with the source and never saved with the scene
struct ProceduralInstanceComponent {
  Entity source_ = entt::null;
};

#endif  // PROCEDURAL_INSTANCE_COMPONENT_H
