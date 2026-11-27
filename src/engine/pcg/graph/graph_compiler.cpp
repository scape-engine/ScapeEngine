#include "graph_compiler.h"

#include <unordered_map>
#include <unordered_set>

#include "node_types.h"

namespace PCG {

namespace {

// Updates remaps when replacing one node with another
void AddRemap(GraphRemappingInfo& remaps, uint32_t old_node_id,
              uint32_t new_node_id, unsigned int output_count) {
  bool existing_remap = false;

  for (PortRemap& remap : remaps.user_to_expanded_ports) {
    if (remap.expanded.node_id == old_node_id) {
      remap.expanded.node_id = new_node_id;
      existing_remap = true;
    }
  }

  for (ExpandedNodeRemap& remap : remaps.expanded_to_user_node_ids) {
    if (remap.expanded_node_id == old_node_id) {
      remap.expanded_node_id = new_node_id;
    }
  }

  if (!existing_remap) {
    for (uint32_t output_index = 0; output_index < output_count;
         ++output_index) {
      PortRemap port_remap;
      port_remap.original =
          ProgramGraph::PortLocation{old_node_id, output_index};
      port_remap.expanded =
          ProgramGraph::PortLocation{new_node_id, output_index};
      remaps.user_to_expanded_ports.push_back(port_remap);
    }
    remaps.expanded_to_user_node_ids.push_back(
        ExpandedNodeRemap{new_node_id, old_node_id});
  }
}

uint32_t GetOriginalNodeId(const GraphRemappingInfo& remaps,
                           uint32_t expanded_node_id) {
  for (const ExpandedNodeRemap& enr : remaps.expanded_to_user_node_ids) {
    if (enr.expanded_node_id == expanded_node_id) {
      return enr.original_node_id;
    }
  }
  return expanded_node_id;
}

// Check if two nodes compute equivalent results
bool IsNodeEquivalent(
    const ProgramGraph& graph, const ProgramGraph::Node& node1,
    const ProgramGraph::Node& node2,
    std::vector<std::pair<uint32_t, uint32_t>>& equivalences) {
  if (node1.id == node2.id) {
    return true;
  }

  for (const auto& eq : equivalences) {
    if (eq.first == node1.id && eq.second == node2.id) {
      return true;
    }
  }

  if (node1.type_id != node2.type_id) {
    return false;
  }

  if (node1.inputs.size() != node2.inputs.size()) {
    return false;
  }

  // Compare parameters
  if (node1.params.size() != node2.params.size()) {
    return false;
  }

  for (size_t i = 0; i < node1.params.size(); ++i) {
    if (node1.params[i] != node2.params[i]) {
      return false;
    }
  }

  // Compare inputs
  for (size_t input_index = 0; input_index < node1.inputs.size();
       ++input_index) {
    const ProgramGraph::Port& input1 = node1.inputs[input_index];
    const ProgramGraph::Port& input2 = node2.inputs[input_index];

    if (input1.connections.size() != input2.connections.size()) {
      return false;
    }

    if (input1.connections.empty()) {
      if (node1.default_inputs[input_index] !=
          node2.default_inputs[input_index]) {
        return false;
      }
    } else {
      const ProgramGraph::PortLocation& src1 = input1.connections[0];
      const ProgramGraph::PortLocation& src2 = input2.connections[0];

      if (src1.port_index != src2.port_index) {
        return false;
      }

      const ProgramGraph::Node& ancestor1 = graph.GetNode(src1.node_id);
      const ProgramGraph::Node& ancestor2 = graph.GetNode(src2.node_id);

      if (!IsNodeEquivalent(graph, ancestor1, ancestor2, equivalences)) {
        equivalences.clear();
        return false;
      }
    }
  }

  equivalences.push_back({node1.id, node2.id});
  return true;
}

// Merge node2 into node1 (redirect node2's outputs to node1)
void MergeNode(ProgramGraph& graph, uint32_t node1_id, uint32_t node2_id,
               GraphRemappingInfo* remap_info) {
  const ProgramGraph::Node& node1 = graph.GetNode(node1_id);
  const ProgramGraph::Node& node2 = graph.GetNode(node2_id);

  assert(node1.type_id == node2.type_id);
  assert(node1.outputs.size() == node2.outputs.size());

  for (size_t output_index = 0; output_index < node2.outputs.size();
       ++output_index) {
    std::vector<ProgramGraph::PortLocation> dsts =
        node2.outputs[output_index].connections;

    for (const ProgramGraph::PortLocation& dst : dsts) {
      graph.Disconnect(
          ProgramGraph::PortLocation{node2_id,
                                     static_cast<uint32_t>(output_index)},
          dst);
      graph.Connect(ProgramGraph::PortLocation{node1_id, static_cast<uint32_t>(
                                                             output_index)},
                    dst);
    }
  }

  if (remap_info != nullptr) {
    AddRemap(*remap_info, node2_id, node1_id,
             static_cast<unsigned int>(node1.outputs.size()));
  }

  graph.RemoveNode(node2_id);
}

// Find and merge equivalent node branches
void MergeEquivalences(ProgramGraph& graph, GraphRemappingInfo* remap_info) {
  std::vector<uint32_t> node_ids;
  graph.GetNodeIds(node_ids);

  std::vector<std::pair<uint32_t, uint32_t>> equivalences;

  for (size_t i = 0; i < node_ids.size(); ++i) {
    const uint32_t node1_id = node_ids[i];
    const ProgramGraph::Node* node1 = graph.TryGetNode(node1_id);
    if (node1 == nullptr) {
      continue;
    }

    for (size_t j = i + 1; j < node_ids.size(); ++j) {
      const uint32_t node2_id = node_ids[j];
      const ProgramGraph::Node* node2 = graph.TryGetNode(node2_id);
      if (node2 == nullptr) {
        continue;
      }

      equivalences.clear();

      if (IsNodeEquivalent(graph, *node1, *node2, equivalences)) {
        for (const auto& eq : equivalences) {
          MergeNode(graph, eq.first, eq.second, remap_info);
        }

        if (graph.TryGetNode(node1_id) == nullptr) {
          break;
        }
      }
    }
  }
}

bool HasAncestor(const ProgramGraph::Node& node) {
  for (const ProgramGraph::Port& input : node.inputs) {
    if (!input.connections.empty()) {
      return true;
    }
  }
  return false;
}

// Evaluate a node with constant inputs to get its output values
CompilationResult EvaluateSingleNode(const ProgramGraph::Node& node,
                                     const NodeType& node_type,
                                     std::vector<float>& output_values) {
  output_values.clear();

  if (node.type_id == static_cast<uint32_t>(NodeTypeID::Constant)) {
    assert(!node.params.empty());
    output_values.push_back(std::get<float>(node.params[0]));
    return CompilationResult::MakeSuccess();
  }

  if (node_type.process_buffer_func == nullptr) {
    return CompilationResult::MakeError(
        "Node has no buffer processing function", node.id);
  }

  // Bake the params as GraphRuntime::Compile does
  std::vector<uint16_t> operations;
  std::vector<GraphRuntime::HeapResource> heap_resources;
  if (node_type.compile_func != nullptr) {
    CompileContext context(operations, heap_resources, node.params);
    node_type.compile_func(context);
    if (context.HasError()) {
      for (GraphRuntime::HeapResource& resource : heap_resources)
        resource.Free();
      return CompilationResult::MakeError(context.GetErrorMessage().c_str(),
                                          node.id);
    }
  }
  if (operations.empty())
    operations.push_back(0);  // no params
  unsigned int pc = 0;
  const std::span<const uint8_t> params =
      GraphRuntime::ReadParams(operations, pc);

  // Build minimal execution context for single evaluation
  // Sized first: the buffers point into it
  std::vector<float> values(node.inputs.size() + node.outputs.size());
  std::vector<GraphRuntime::Buffer> buffers;
  std::vector<uint32_t> input_addresses;
  std::vector<uint32_t> output_addresses;

  // Set up input buffers with default values
  for (size_t i = 0; i < node.inputs.size(); ++i) {
    values[i] = node.default_inputs[i];
    GraphRuntime::Buffer buffer;
    buffer.data = &values[i];
    buffer.is_constant = true;
    buffer.constant_value = values[i];
    buffers.push_back(buffer);
    input_addresses.push_back(static_cast<uint32_t>(i));
  }

  // Set up output buffers
  for (size_t i = 0; i < node.outputs.size(); ++i) {
    const size_t value_index = node.inputs.size() + i;
    GraphRuntime::Buffer buffer;
    buffer.data = &values[value_index];
    buffer.is_constant = false;
    buffers.push_back(buffer);
    output_addresses.push_back(static_cast<uint32_t>(value_index));
  }

  // Execute node
  std::unordered_map<std::string, std::shared_ptr<void>> resources;
  GraphRuntime::ProcessContext ctx(input_addresses, output_addresses, params,
                                   buffers, resources);
  node_type.process_buffer_func(ctx);
  for (GraphRuntime::HeapResource& resource : heap_resources)
    resource.Free();

  // Extract outputs
  for (uint32_t oi : output_addresses) {
    output_values.push_back(values[oi]);
  }

  return CompilationResult::MakeSuccess();
}

// Fold constant expressions
CompilationResult ReduceConstants(ProgramGraph& graph,
                                  const NodeTypeDB& type_db) {
  std::vector<uint32_t> node_ids;
  std::vector<float> output_values;

  bool keep_going = true;
  while (keep_going) {
    keep_going = false;
    node_ids.clear();
    graph.GetNodeIds(node_ids);

    for (const uint32_t node_id : node_ids) {
      const ProgramGraph::Node& node = graph.GetNode(node_id);

      if (node.outputs.empty()) {
        continue;
      }

      const NodeType& node_type =
          type_db.Get(static_cast<NodeTypeID>(node.type_id));

      // Skip output nodes
      if (node_type.category == Category::OUTPUT) {
        continue;
      }

      // Skip input nodes
      if (node_type.category == Category::INPUT) {
        continue;
      }

      // Skip nodes with connected inputs
      if (HasAncestor(node)) {
        continue;
      }

      const CompilationResult eval_result =
          EvaluateSingleNode(node, node_type, output_values);
      if (!eval_result.success) {
        return eval_result;
      }

      assert(output_values.size() == node.outputs.size());

      // Propagate constant values to connected inputs
      for (size_t output_index = 0; output_index < output_values.size();
           ++output_index) {
        const ProgramGraph::Port& output = node.outputs[output_index];
        const float value = output_values[output_index];

        for (const ProgramGraph::PortLocation& dst_loc : output.connections) {
          ProgramGraph::Node& dst_node = graph.GetNode(dst_loc.node_id);
          dst_node.default_inputs[dst_loc.port_index] = value;
        }
      }

      graph.RemoveNode(node_id);
      keep_going = true;
    }
  }

  return CompilationResult::MakeSuccess();
}

}  // namespace

CompilationResult ExpandGraph(const ProgramGraph& graph,
                              ProgramGraph& expanded_graph,
                              const NodeTypeDB& type_db,
                              GraphRemappingInfo* remap_info, bool debug) {
  // Copy graph for modifications
  expanded_graph.CopyFrom(graph);

  // Fold constant expressions (skip in debug for inspection)
  if (!debug) {
    CompilationResult reduction_result =
        ReduceConstants(expanded_graph, type_db);
    if (!reduction_result.success) {
      if (remap_info != nullptr) {
        reduction_result.node_id =
            GetOriginalNodeId(*remap_info, reduction_result.node_id);
      }
      return reduction_result;
    }
  }

  // Merge equivalent branches
  MergeEquivalences(expanded_graph, remap_info);

  return CompilationResult::MakeSuccess();
}

}  // namespace PCG