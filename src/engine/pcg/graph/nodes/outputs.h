#ifndef PCG_NODES_OUTPUTS_H
#define PCG_NODES_OUTPUTS_H

#include <span>

#include "../node_types.h"

namespace PCG {

// GraphRuntime reads an output's input straight into the Sample; it runs
// nothing
// TODO: outputs beyond height (godot_voxel's OutputWeight, OutputType,
// OutputSingleTexture: here surface data such as Sample::properties)
inline void RegisterOutputNodes(std::span<NodeType> types) {
  NodeType& t = types[static_cast<size_t>(NodeTypeID::OutputHeight)];
  t.name = "Output Height";
  t.category = Category::OUTPUT;
  t.inputs = {{"height"}};
}

}  // namespace PCG

#endif  // PCG_NODES_OUTPUTS_H
