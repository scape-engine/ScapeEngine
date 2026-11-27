#ifndef PCG_NODES_MATH_FUNCS_H
#define PCG_NODES_MATH_FUNCS_H

#include <algorithm>
#include <cmath>
#include <span>

#include "../node_types.h"

namespace PCG {

// godot_voxel's math_funcs.h: functions of one value, and the conversions
// (clamping, remapping, snapping) with their bounds as inputs or params
inline void RegisterMathFuncsNodes(std::span<NodeType> types) {
  using Context = GraphRuntime::ProcessContext;
  using Type = NodeType::Param::Type;
  const auto add = [&](NodeTypeID id, const char* name, Category category,
                       std::vector<NodeType::Port> inputs,
                       GraphRuntime::ProcessBufferFunc func) -> NodeType& {
    NodeType& t = types[static_cast<size_t>(id)];
    t.name = name;
    t.category = category;
    t.inputs = std::move(inputs);
    t.outputs = {{"out"}};
    t.process_buffer_func = func;
    return t;
  };
  add(NodeTypeID::Sin, "Sin", Category::MATH, {{"x"}}, [](Context& ctx) {
    ctx.SetOutput(0, std::sin(ctx.GetInput(0)));
  });
  add(NodeTypeID::Floor, "Floor", Category::MATH, {{"x"}}, [](Context& ctx) {
    ctx.SetOutput(0, std::floor(ctx.GetInput(0)));
  });
  add(NodeTypeID::Abs, "Abs", Category::MATH, {{"x"}}, [](Context& ctx) {
    ctx.SetOutput(0, std::abs(ctx.GetInput(0)));
  });
  add(NodeTypeID::Sqrt, "Sqrt", Category::MATH, {{"x"}}, [](Context& ctx) {
    ctx.SetOutput(0, std::sqrt(std::max(ctx.GetInput(0), 0.0f)));
  });
  add(NodeTypeID::Fract, "Fract", Category::MATH, {{"x"}}, [](Context& ctx) {
    const float x = ctx.GetInput(0);
    ctx.SetOutput(0, x - std::floor(x));
  });
  add(NodeTypeID::Wrap, "Wrap", Category::MATH, {{"x"}, {"length", 1.0f}},
      [](Context& ctx) {
        const float x = ctx.GetInput(0);
        const float length = ctx.GetInput(1);
        ctx.SetOutput(0, x - length * std::floor(x / length));
      });
  add(NodeTypeID::Min, "Min", Category::MATH, {{"a"}, {"b"}},
      [](Context& ctx) {
        ctx.SetOutput(0, std::min(ctx.GetInput(0), ctx.GetInput(1)));
      });
  add(NodeTypeID::Max, "Max", Category::MATH, {{"a"}, {"b"}},
      [](Context& ctx) {
        ctx.SetOutput(0, std::max(ctx.GetInput(0), ctx.GetInput(1)));
      });
  add(NodeTypeID::Power, "Pow", Category::MATH, {{"x"}, {"p", 2.0f}},
      [](Context& ctx) {
        ctx.SetOutput(0, std::pow(ctx.GetInput(0), ctx.GetInput(1)));
      });
  {
    // x^power, as godot_voxel's range analysis and shaders do (its CPU path
    // squares power - 1 times instead)
    NodeType& t = add(NodeTypeID::PowerInt, "Powi", Category::MATH, {{"x"}},
                      [](Context& ctx) {
                        const float x = ctx.GetInput(0);
                        const int power = ctx.GetParams<int>();
                        float v = 1.0f;
                        for (int i = 0; i < power; ++i)
                          v *= x;
                        ctx.SetOutput(0, v);
                      });
    t.params = {{"power", Type::Int, 2, 0.0f, 8.0f, true}};
    t.compile_func = [](CompileContext& ctx) {
      const int power = std::get<int>(ctx.GetParam(0));
      if (power < 0)
        ctx.MakeError("Power cannot be negative");
      else
        ctx.SetParams(power);
    };
  }

  // Rounded to the nearest multiple of step; as is where step is zero
  add(NodeTypeID::Stepify, "Stepify", Category::CONVERT,
      {{"x"}, {"step", 1.0f}}, [](Context& ctx) {
        const float x = ctx.GetInput(0);
        const float step = ctx.GetInput(1);
        ctx.SetOutput(
            0, step == 0.0f ? x : std::floor(x / step + 0.5f) * step);
      });
  // Not std::clamp: bounds the wrong way round are no error
  add(NodeTypeID::Clamp, "Clamp", Category::CONVERT,
      {{"x"}, {"min", -1.0f}, {"max", 1.0f}}, [](Context& ctx) {
        const float x = ctx.GetInput(0);
        ctx.SetOutput(0, std::min(std::max(x, ctx.GetInput(1)),
                                  ctx.GetInput(2)));
      });
  {
    struct Bounds {
      float min;
      float max;
    };
    NodeType& t = add(NodeTypeID::ClampConstant, "ClampC", Category::CONVERT,
                      {{"x"}}, [](Context& ctx) {
                        const Bounds& b = ctx.GetParams<Bounds>();
                        const float x = ctx.GetInput(0);
                        ctx.SetOutput(0, std::min(std::max(x, b.min), b.max));
                      });
    t.params = {{"min", Type::Float, -1.0f}, {"max", Type::Float, 1.0f}};
    t.compile_func = [](CompileContext& ctx) {
      ctx.SetParams(Bounds{std::get<float>(ctx.GetParam(0)),
                           std::get<float>(ctx.GetParam(1))});
    };
  }
  add(NodeTypeID::Mix, "Mix", Category::CONVERT,
      {{"a"}, {"b"}, {"ratio"}}, [](Context& ctx) {
        const float a = ctx.GetInput(0);
        ctx.SetOutput(0, a + (ctx.GetInput(1) - a) * ctx.GetInput(2));
      });
  {
    // [min0, max0] onto [min1, max1], as the line a * x + b
    struct Line {
      float a;
      float b;
    };
    NodeType& t = add(NodeTypeID::Remap, "Remap", Category::CONVERT, {{"x"}},
                      [](Context& ctx) {
                        const Line& line = ctx.GetParams<Line>();
                        ctx.SetOutput(0, line.a * ctx.GetInput(0) + line.b);
                      });
    t.params = {{"min0", Type::Float, -1.0f},
                {"max0", Type::Float, 1.0f},
                {"min1", Type::Float, -1.0f},
                {"max1", Type::Float, 1.0f}};
    t.compile_func = [](CompileContext& ctx) {
      const float min0 = std::get<float>(ctx.GetParam(0));
      const float max0 = std::get<float>(ctx.GetParam(1));
      const float min1 = std::get<float>(ctx.GetParam(2));
      const float max1 = std::get<float>(ctx.GetParam(3));
      if (min0 == max0) {
        ctx.MakeError("min0 and max0 can't be equal");
        return;
      }
      const float a = (max1 - min1) / (max0 - min0);
      ctx.SetParams(Line{a, min1 - a * min0});
    };
  }
  {
    struct Edges {
      float edge0;
      float edge1;
    };
    NodeType& t = add(NodeTypeID::Smoothstep, "Smoothstep", Category::CONVERT,
                      {{"x"}}, [](Context& ctx) {
                        const Edges& e = ctx.GetParams<Edges>();
                        float s = (ctx.GetInput(0) - e.edge0) /
                                  (e.edge1 - e.edge0);
                        s = std::min(std::max(s, 0.0f), 1.0f);
                        ctx.SetOutput(0, s * s * (3.0f - 2.0f * s));
                      });
    t.params = {{"edge0", Type::Float, 0.0f}, {"edge1", Type::Float, 1.0f}};
    t.compile_func = [](CompileContext& ctx) {
      const Edges e{std::get<float>(ctx.GetParam(0)),
                    std::get<float>(ctx.GetParam(1))};
      if (e.edge0 == e.edge1)
        ctx.MakeError("edge0 and edge1 can't be equal");
      else
        ctx.SetParams(e);
    };
  }
}

}  // namespace PCG

#endif  // PCG_NODES_MATH_FUNCS_H
