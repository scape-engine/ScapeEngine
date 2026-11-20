// Checks must run in release builds too
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <iostream>

#include <glm/glm.hpp>

#include "terrain/terrain_colour.h"
#include "terrain/terrain_tiles.h"

// The terrain shader's tiles, compiled as C++: GLSL's float and vectors
// become doubles, so the check measures the maths, not float rounding. The
// shader's textures are made-up values (below)
namespace TerrainShader {
using namespace glm;
using vec2 = glm::dvec2;
using vec3 = glm::dvec3;
using vec4 = glm::dvec4;
#define float double
vec4 TestSample(float tile, float u, float v);
vec4 TestTarget(float tile);
vec4 TestAverage(float tile);
#define TERRAIN_OUT(T) T&
#define TERRAIN_SAMPLE(tile, u, v) TestSample(tile, u, v)
#define TERRAIN_TARGET(tile) TestTarget(tile)
#define TERRAIN_AVERAGE(tile) TestAverage(tile)
#define TERRAIN_HAS_IMAGE(tile) 1.0
#include "assets/shaders/includes/terrain_tiles.sh"
#undef TERRAIN_HAS_IMAGE
#undef TERRAIN_AVERAGE
#undef TERRAIN_TARGET
#undef TERRAIN_SAMPLE
#undef TERRAIN_OUT
#undef float
}  // namespace TerrainShader

namespace Tests {

// Made-up tile images: smooth in u, v, so float and double coordinates read
// the same, saturation and value away from 0
static Worldgen::TerrainTexel _Texel(int tile, double u, double v) {
  Worldgen::TerrainTexel texel;
  texel.hsv[0] = static_cast<float>(0.5 + 0.45 * std::sin(u * 1.3 + tile * 0.7));
  texel.hsv[1] = static_cast<float>(0.55 + 0.35 * std::sin(v * 0.9 + tile));
  texel.hsv[2] = static_cast<float>(0.55 + 0.35 * std::cos(u * 0.7 + v * 1.1));
  texel.height = static_cast<float>(0.5 + 0.4 * std::sin(u + v * 1.7 + tile * 0.3));
  return texel;
}

// Made-up tile colours and image means
static Worldgen::TerrainColourTarget _Colours() {
  Worldgen::TerrainColourTarget colours;
  for (int t = 0; t < 23; ++t) {
    const float target[4] = {std::fmod(t * 0.137f, 1.0f), 0.3f + 0.02f * t,
                             0.4f + 0.02f * t, 0.1f * std::sin(t * 1.0f)};
    const float average[4] = {std::fmod(t * 0.29f, 1.0f), 0.5f, 0.5f,
                              0.3f * std::cos(t * 1.0f)};
    for (int c = 0; c < 4; ++c) {
      colours.target[t][c] = target[c];
      colours.average[t][c] = average[c];
    }
  }
  return colours;
}
static const Worldgen::TerrainColourTarget k_colours = _Colours();

}  // namespace Tests

namespace TerrainShader {
glm::dvec4 TestSample(double tile, double u, double v) {
  const Worldgen::TerrainTexel texel = Tests::_Texel(static_cast<int>(tile), u, v);
  float rgb[4];
  Worldgen::TerrainColour::HsvToRgb(texel.hsv, rgb);
  return glm::dvec4(rgb[0], rgb[1], rgb[2], texel.height);
}
glm::dvec4 TestTarget(double tile) {
  const float* t = Tests::k_colours.target[static_cast<int>(tile)];
  return glm::dvec4(t[0], t[1], t[2], t[3]);
}
glm::dvec4 TestAverage(double tile) {
  const float* a = Tests::k_colours.average[static_cast<int>(tile)];
  return glm::dvec4(a[0], a[1], a[2], a[3]);
}
}  // namespace TerrainShader

namespace Tests {

// The shader picks the same tiles with the same weights as worldgen's
// reference (Worldgen::TerrainTiles, neutral heights)
static int _CheckTileWeights() {
  int checked = 0;
  const glm::dvec4 neutral(0.5);
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
                                            noise, neutral, neutral, tiles[0],
                                            tiles[1], weights[0], weights[1]);
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
  return checked;
}

// The shader's colour from the tile images matches worldgen's colour pass
// (Worldgen::TerrainColour::Shade): heights, blends and recolouring
static int _CheckShade() {
  int checked = 0;
  for (int a = 0; a < 23; ++a) {
    for (int n = 0; n < 12; ++n) {
      for (float blend : {0.0f, 0.3f, 0.6f, 1.0f}) {
        for (float noise : {0.1f, 0.45f, 0.8f}) {
          const float attribute0 = a + 0.1f, attribute1 = (a * 5 + 2) % 23;
          // Normals from mostly one axis to between all three
          glm::vec3 normal = glm::normalize(glm::vec3(
              std::sin(n * 1.7f), std::cos(n * 0.9f) + 0.2f, std::sin(n * 2.3f + 1.0f)));
          const float up = 0.15f + 0.07f * n;
          const float position[3] = {37.0f * n - 120.0f, 410.0f + 3.0f * a,
                                     -55.0f + 11.0f * n};

          int slot[8];
          Worldgen::TerrainTiles::SlotTiles(attribute0, attribute1, slot);
          float uv[3];
          Worldgen::TerrainColour::Uv(position, uv);
          Worldgen::TerrainTexels texels;
          for (int axis = 0; axis < 3; ++axis) {
            float u, v;
            Worldgen::TerrainColour::AxisUv(axis, uv, u, v);
            for (int k = 0; k < 8; ++k)
              texels.axis[axis][k] = _Texel(slot[k], u, v);
          }
          float expected[3];
          Worldgen::TerrainColour::Shade(attribute0, attribute1, blend,
                                         &normal.x, up, noise, texels,
                                         k_colours, expected);
          const glm::dvec3 colour = TerrainShader::TerrainShade(
              attribute0, attribute1, blend, glm::dvec3(normal), up, noise,
              glm::dvec3(position[0], position[1], position[2]));
          // The reference rounds in float, which the height blends (x 10) and
          // the projection (|n|^64) magnify to about 1.5e-4 at worst
          for (int c = 0; c < 3; ++c)
            assert(std::abs(colour[c] - expected[c]) < 5e-4);
          ++checked;
        }
      }
    }
  }

  // Colour conversions, and tiles without an image show their flat colour
  for (int i = 0; i < 64; ++i) {
    const float rgb[3] = {std::fmod(i * 0.37f, 1.0f), std::fmod(i * 0.61f, 1.0f),
                          std::fmod(i * 0.13f, 1.0f)};
    float hsv[4];
    Worldgen::TerrainColour::RgbToHsv(rgb, hsv);
    const glm::dvec3 shader = TerrainShader::TerrainRgbToHsv(glm::dvec3(rgb[0], rgb[1], rgb[2]));
    for (int c = 0; c < 3; ++c)
      assert(std::abs(shader[c] - hsv[c]) < 1e-5);
    const glm::dvec3 flat = TerrainShader::TerrainTexelColour(
        glm::dvec4(0.9, 0.1, 0.2, 0.5), glm::dvec4(shader, 0.0),
        glm::dvec4(0.3, 0.3, 0.3, 0.0), 0.0);
    for (int c = 0; c < 3; ++c)
      assert(std::abs(flat[c] - rgb[c]) < 1e-5);
  }
  return checked;
}

void TestTerrainTiles() {
  const int weights = _CheckTileWeights();
  const int shades = _CheckShade();
  std::cout << "Terrain tiles: OK (" << weights << " weights, " << shades
            << " colours)" << std::endl;
}

}  // namespace Tests
