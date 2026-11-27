#ifndef PCG_NODES_INPUTS_H
#define PCG_NODES_INPUTS_H

#include <span>

#include "../node_types.h"

namespace PCG {

// GraphRuntime::Compile turns these into bindings (set per sample from the
// position) and constants; they never run
// TODO: Input Z, once graphs make volumes (godot_voxel's InputZ, InputSDF)
inline void RegisterInputNodes(std::span<NodeType> types) {
  {
    NodeType& t = types[static_cast<size_t>(NodeTypeID::InputX)];
    t.name = "Input X";
    t.category = Category::INPUT;
    t.outputs = {{"x"}};
  }
  {
    NodeType& t = types[static_cast<size_t>(NodeTypeID::InputY)];
    t.name = "Input Y";
    t.category = Category::INPUT;
    t.outputs = {{"y"}};
  }
  {
    NodeType& t = types[static_cast<size_t>(NodeTypeID::Constant)];
    t.name = "Constant";
    t.category = Category::INPUT;
    t.outputs = {{"value"}};
    t.params = {{"value", NodeType::Param::Type::Float, 0.0f}};
  }
}

}  // namespace PCG

#endif  // PCG_NODES_INPUTS_H
