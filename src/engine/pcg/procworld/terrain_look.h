#ifndef TERRAIN_LOOK_H
#define TERRAIN_LOOK_H

#include "engine/renderer/material/terrain_material.h"

namespace ProcWorld {

// Made-up tile colours for terrain without its own: worldgen's tile groups
// put the steep looks on even tiles (rock) and the flat ones on odd tiles
// (grass, sand)
inline TerrainMaterialData DefaultTerrainLook() {
  TerrainMaterialData look;
  look.name = "Terrain";
  for (int i = 0; i < TerrainMaterialData::tile_count; ++i) {
    const float shade = 0.85f + 0.05f * static_cast<float>(i % 4);
    look.tile_colours[i] = (i % 2 == 0 ? glm::vec3(0.42f, 0.38f, 0.34f)
                                       : glm::vec3(0.36f, 0.52f, 0.24f)) *
                           shade;
  }
  return look;
}

}  // namespace ProcWorld

#endif  // TERRAIN_LOOK_H
