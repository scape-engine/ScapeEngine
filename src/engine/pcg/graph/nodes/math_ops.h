#ifndef PCG_NODES_MATH_OPS_H
#define PCG_NODES_MATH_OPS_H

#include <span>

#include "../node_types.h"

namespace PCG {

// godot_voxel's math_ops.h: the four operators
inline void RegisterMathOpsNodes(std::span<NodeType> types) {
  using Context = GraphRuntime::ProcessContext;
  const auto add = [&](NodeTypeID id, const char* name, float b,
                       GraphRuntime::ProcessBufferFunc func) {
    NodeType& t = types[static_cast<size_t>(id)];
    t.name = name;
    t.category = Category::MATH;
    t.inputs = {{"a"}, {"b", b}};
    t.outputs = {{"out"}};
    t.process_buffer_func = func;
  };

  add(NodeTypeID::Add, "Add", 0.0f, [](Context& ctx) {
    ctx.SetOutput(0, ctx.GetInput(0) + ctx.GetInput(1));
  });
  add(NodeTypeID::Subtract, "Subtract", 0.0f, [](Context& ctx) {
    ctx.SetOutput(0, ctx.GetInput(0) - ctx.GetInput(1));
  });
  add(NodeTypeID::Multiply, "Multiply", 0.0f, [](Context& ctx) {
    ctx.SetOutput(0, ctx.GetInput(0) * ctx.GetInput(1));
  });
  // Zero where b is zero, not NaN
  add(NodeTypeID::Divide, "Divide", 1.0f, [](Context& ctx) {
    const float b = ctx.GetInput(1);
    ctx.SetOutput(0, b == 0.0f ? 0.0f : ctx.GetInput(0) / b);
  });
}

}  // namespace PCG

#endif  // PCG_NODES_MATH_OPS_H
