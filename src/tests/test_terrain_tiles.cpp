// Checks must run in release builds too
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <iostream>

#include <glm/glm.hpp>

#include "terrain/terrain_tiles.h"

// The terrain shader's tile picking, compiled as C++: GLSL's float and vec4
// become doubles, so the check measures the maths, not float rounding
namespace TerrainShader {
using namespace glm;
using vec4 = glm::dvec4;
#define float double
#define TERRAIN_OUT(T) T&
#include "assets/shaders/includes/terrain_tiles.sh"
#undef TERRAIN_OUT
#undef float
}  // namespace TerrainShader

namespace Tests {

// The shader picks the same tiles with the same weights as worldgen's
// reference (Worldgen::TerrainTiles, neutral heights)
void TestTerrainTiles() {
  int checked = 0;
  for (int a = 0; a < 23; ++a) {
    for (float up : {0.0f, 0.2f, 0.45f, 0.6f, 0.8f, 1.0f}) {
      for (float blend : {0.0f, 0.03f, 0.4f, 0.5f, 0.97f, 1.0f}) {
        for (float noise : {0.0f, 0.3f, 0.5f, 0.52f, 1.0f}) {
          // Tile attributes carry 0.1 of the group's material
          const float attribute0 = a + 0.1f, attribute1 = (a * 7 + 3) % 23;
          const Worldgen::TerrainTileWeights expected =
              Worldgen::TerrainTiles::Weights(attribute0, attribute1, blend,
                                              up, noise);
          glm::dvec4 tiles[2], weights[2];
          TerrainShader::TerrainTileWeights(attribute0, attribute1, blend, up,
                                            noise, tiles[0], tiles[1],
                                            weights[0], weights[1]);
          for (int k = 0; k < 8; ++k) {
            const double weight = weights[k / 4][k % 4];
            if (expected.weight[k] > 0.0f || weight > 0.0)
              assert(static_cast<int>(tiles[k / 4][k % 4]) ==
                     expected.tile[k]);
            assert(std::abs(weight - expected.weight[k]) < 1e-4);
          }
          ++checked;
        }
      }
    }
  }
  std::cout << "Terrain tiles: OK (" << checked << " points)" << std::endl;
}

}  // namespace Tests
