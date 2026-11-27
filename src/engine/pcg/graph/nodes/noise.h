#include "../node_types.h"

#include <FastNoise/FastNoise.h>
#include <string>

#include "../../operators/fbm.h"
#include "engine/math/noise/OpenSimplex2S.hpp"

namespace PCG {
// TODO: add domain warp, fastnoise gradient 2d, fastnoise 3d, fastnoise 2d,
// noise 3d/2d

inline void RegisterNoiseNodes(std::span<NodeType> types) {

  // OpenSimplex2 with derivatives
  {
    NodeType& t = types[static_cast<size_t>(NodeTypeID::OpenSimplex2_Deriv)];
    t.name = "OpenSimplex2 (Derivatives)";
    t.category = Category::NOISE;
    t.inputs = {{"x", 0.0f}, {"y", 0.0f}};
    t.outputs = {{"value"}, {"dx"}, {"dy"}};
    t.params = {
        {"seed", NodeType::Param::Type::Int, 12345},
        {"frequency", NodeType::Param::Type::Float, 0.01f, 0.001f, 1.0f, true}};

    t.compile_func = [](CompileContext& ctx) {
      struct Params {
        int seed;
        float frequency;
      };
      Params p;
      p.seed = std::get<int>(ctx.GetParam(0));
      p.frequency = std::get<float>(ctx.GetParam(1));
      ctx.SetParams(p);
    };

    t.process_buffer_func = [](GraphRuntime::ProcessContext& ctx) {
      struct Params {
        int seed;
        float frequency;
      };
      const Params& p = ctx.GetParams<Params>();

      // One per seed: nodes and graphs on a thread share the cache
      auto* simplex = ctx.GetOrCreateResource<OpenSimplex2S>(
          "simplex" + std::to_string(p.seed), p.seed);

      float x = ctx.GetInput(0) * p.frequency;
      float y = ctx.GetInput(1) * p.frequency;

      double value, dx, dy;
      simplex->noise2_deriv(x, y, value, dx, dy);

      ctx.SetOutput(0, static_cast<float>(value));
      ctx.SetOutput(1, static_cast<float>(dx) * p.frequency);
      ctx.SetOutput(2, static_cast<float>(dy) * p.frequency);
    };
  }

  // FastNoise2 Simplex
  {
    NodeType& t = types[static_cast<size_t>(NodeTypeID::FN2_Simplex2D)];
    t.name = "FN2 Simplex 2D";
    t.category = Category::NOISE;
    t.inputs = {{"x", 0.0f}, {"y", 0.0f}};
    t.outputs = {{"value"}};
    t.params = {
        {"seed", NodeType::Param::Type::Int, 12345},
        {"frequency", NodeType::Param::Type::Float, 0.01f, 0.001f, 1.0f, true}};

    t.compile_func = [](CompileContext& ctx) {
      struct Params {
        int seed;
        float frequency;
      };
      Params p;
      p.seed = std::get<int>(ctx.GetParam(0));
      p.frequency = std::get<float>(ctx.GetParam(1));
      ctx.SetParams(p);
    };

    t.process_buffer_func = [](GraphRuntime::ProcessContext& ctx) {
      struct Params {
        int seed;
        float frequency;
      };
      const Params& p = ctx.GetParams<Params>();

      struct SimplexHolder {
        FastNoise::SmartNode<FastNoise::Simplex> node =
            FastNoise::New<FastNoise::Simplex>();
      };
      auto* holder = ctx.GetOrCreateResource<SimplexHolder>("fn2_simplex");

      float x = ctx.GetInput(0) * p.frequency;
      float y = ctx.GetInput(1) * p.frequency;

      ctx.SetOutput(0, holder->node->GenSingle2D(x, y, p.seed));
    };
  }

  // UberFBM - full erosion pipeline
  {
    NodeType& t = types[static_cast<size_t>(NodeTypeID::UberFBM)];
    t.name = "Uber FBM";
    t.category = Category::NOISE;
    t.inputs = {{"x", 0.0f}, {"y", 0.0f}};
    t.outputs = {{"value"}, {"dx"}, {"dy"}};
    t.params = {
        {"seed", NodeType::Param::Type::Int, 12345},
        {"octaves", NodeType::Param::Type::Int, 6, 1, 12, true},
        {"frequency", NodeType::Param::Type::Float, 0.01f, 0.001f, 1.0f, true},
        {"lacunarity", NodeType::Param::Type::Float, 2.0f, 1.0f, 4.0f, true},
        {"gain", NodeType::Param::Type::Float, 0.5f, 0.0f, 1.0f, true},
        {"sharpness", NodeType::Param::Type::Float, 0.0f, -1.0f, 1.0f, true},
        {"slope_erosion", NodeType::Param::Type::Float, 0.0f, 0.0f, 1.0f, true},
        {"ridge_erosion", NodeType::Param::Type::Float, 0.0f, 0.0f, 1.0f, true},
        {"altitude_erosion", NodeType::Param::Type::Float, 0.0f, 0.0f, 1.0f,
         true},
        {"perturb", NodeType::Param::Type::Float, 0.0f, 0.0f, 2.0f, true}};

    t.compile_func = [](CompileContext& ctx) {
      struct Params {
        int seed;
        int octaves;
        float frequency;
        float lacunarity;
        float gain;
        float sharpness;
        float slope_erosion;
        float ridge_erosion;
        float altitude_erosion;
        float perturb;
      };
      Params p;
      p.seed = std::get<int>(ctx.GetParam(0));
      p.octaves = std::get<int>(ctx.GetParam(1));
      p.frequency = std::get<float>(ctx.GetParam(2));
      p.lacunarity = std::get<float>(ctx.GetParam(3));
      p.gain = std::get<float>(ctx.GetParam(4));
      p.sharpness = std::get<float>(ctx.GetParam(5));
      p.slope_erosion = std::get<float>(ctx.GetParam(6));
      p.ridge_erosion = std::get<float>(ctx.GetParam(7));
      p.altitude_erosion = std::get<float>(ctx.GetParam(8));
      p.perturb = std::get<float>(ctx.GetParam(9));
      ctx.SetParams(p);
    };

    t.process_buffer_func = [](GraphRuntime::ProcessContext& ctx) {
      struct Params {
        int seed;
        int octaves;
        float frequency;
        float lacunarity;
        float gain;
        float sharpness;
        float slope_erosion;
        float ridge_erosion;
        float altitude_erosion;
        float perturb;
      };
      const Params& p = ctx.GetParams<Params>();

      auto* uber = ctx.GetOrCreateResource<UberFBM>(
          "uber_fbm" + std::to_string(p.seed), p.seed);

      UberFBMParams params;
      params.octaves = p.octaves;
      params.lacunarity = p.lacunarity;
      params.gain = p.gain;
      params.sharpness = p.sharpness;
      params.slope_erosion = p.slope_erosion;
      params.ridge_erosion = p.ridge_erosion;
      params.altitude_erosion = p.altitude_erosion;
      params.perturb = p.perturb;

      float x = ctx.GetInput(0) * p.frequency;
      float y = ctx.GetInput(1) * p.frequency;

      auto result = uber->Eval(x, y, p.seed, params);

      ctx.SetOutput(0, result.value);
      ctx.SetOutput(1, result.derivative.x);
      ctx.SetOutput(2, result.derivative.y);
    };
  }
}

}  // namespace PCG