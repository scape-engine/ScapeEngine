#include "graph_runtime.h"

#include <algorithm>
#include <cassert>
#include <unordered_set>

#include "graph_compiler.h"
#include "node_types.h"

namespace PCG {

GraphRuntime::GraphRuntime() = default;

GraphRuntime::~GraphRuntime() {
  Clear();
}

void GraphRuntime::Clear() {
  program_.Clear();
}

void GraphRuntime::State::Clear() {
  for (BufferData& bd : buffer_datas_) {
    delete[] bd.data;
    bd.data = nullptr;
    bd.capacity = 0;
  }
  buffer_datas_.clear();
  buffers_.clear();
  buffer_size_ = 0;
  buffer_capacity_ = 0;
  resources.clear();
}

CompilationResult GraphRuntime::Compile(const ProgramGraph& graph) {
  Clear();

  const NodeTypeDB& type_db = NodeTypeDB::Instance();

  // Expand and optimize graph
  ProgramGraph expanded_graph;
  GraphRemappingInfo remap_info;

  CompilationResult expand_result =
      ExpandGraph(graph, expanded_graph, type_db, &remap_info, false);
  if (!expand_result.success) {
    return expand_result;
  }

  // Compute execution order using topological sort
  std::vector<uint32_t> order;
  std::unordered_set<uint32_t> visited;
  std::unordered_set<uint32_t> in_stack;

  // Find output nodes as terminals
  std::vector<uint32_t> terminal_ids;
  expanded_graph.ForEachNode([&](const ProgramGraph::Node& node) {
    const NodeType& type = type_db.Get(static_cast<NodeTypeID>(node.type_id));
    if (type.category == Category::OUTPUT) {
      terminal_ids.push_back(node.id);
    }
  });

  if (terminal_ids.empty()) {
    return CompilationResult::MakeError("Graph has no output nodes");
  }

  // Topological sort helper (DFS post-order)
  std::function<bool(uint32_t)> visit = [&](uint32_t node_id) -> bool {
    if (in_stack.count(node_id)) {
      return false;  // Cycle detected
    }
    if (visited.count(node_id)) {
      return true;
    }

    in_stack.insert(node_id);

    const ProgramGraph::Node& node = expanded_graph.GetNode(node_id);
    for (const ProgramGraph::Port& input : node.inputs) {
      for (const ProgramGraph::PortLocation& src : input.connections) {
        if (!visit(src.node_id)) {
          return false;
        }
      }
    }

    in_stack.erase(node_id);
    visited.insert(node_id);
    order.push_back(node_id);
    return true;
  };

  for (uint32_t terminal_id : terminal_ids) {
    if (!visit(terminal_id)) {
      return CompilationResult::MakeError("Graph contains a cycle");
    }
  }

  // Memory allocation helper
  struct MemoryHelper {
    std::vector<BufferSpec>& specs;
    uint16_t next_address = 0;

    uint16_t AddBinding() {
      uint16_t addr = next_address++;
      BufferSpec bs;
      bs.address = addr;
      bs.is_binding = true;
      bs.is_constant = false;
      bs.users_count = 0;
      specs.push_back(bs);
      return addr;
    }

    uint16_t AddVar() {
      uint16_t addr = next_address++;
      BufferSpec bs;
      bs.address = addr;
      bs.is_binding = false;
      bs.is_constant = false;
      bs.users_count = 0;
      specs.push_back(bs);
      return addr;
    }

    uint16_t AddConstant(float value) {
      uint16_t addr = next_address++;
      BufferSpec bs;
      bs.address = addr;
      bs.is_binding = false;
      bs.is_constant = true;
      bs.constant_value = value;
      bs.users_count = 0;
      specs.push_back(bs);
      return addr;
    }
  };

  MemoryHelper mem{program_.buffer_specs};

  // Maps node output ports to buffer addresses
  std::unordered_map<uint64_t, uint16_t> port_to_address;

  auto MakePortKey = [](uint32_t node_id, uint32_t port_index) -> uint64_t {
    return (static_cast<uint64_t>(node_id) << 32) | port_index;
  };

  // Track input bindings by type
  std::unordered_map<uint32_t, uint16_t> input_type_to_address;

  // Process nodes in execution order
  for (uint32_t node_id : order) {
    const ProgramGraph::Node& node = expanded_graph.GetNode(node_id);
    const NodeType& type = type_db.Get(static_cast<NodeTypeID>(node.type_id));

    // Handle input nodes - allocate bindings
    if (type.category == Category::INPUT) {
      uint16_t addr;

      if (node.type_id == static_cast<uint32_t>(NodeTypeID::Constant)) {
        assert(!node.params.empty());
        float value = std::get<float>(node.params[0]);
        addr = mem.AddConstant(value);
      } else {
        // Check if there is a binding for this input type
        auto it = input_type_to_address.find(node.type_id);
        if (it != input_type_to_address.end()) {
          addr = it->second;
        } else {
          addr = mem.AddBinding();
          input_type_to_address[node.type_id] = addr;
          program_.inputs.push_back({addr, node.type_id});
        }
      }

      port_to_address[MakePortKey(node_id, 0)] = addr;
      continue;
    }

    // Build operation entry
    ExecutionMap::OperationInfo op_info;
    op_info.address = static_cast<uint16_t>(program_.operations.size());
    program_.default_execution_map.operations.push_back(op_info);

    // Encode: [type_id, input_count, input_addrs..., output_count,
    // output_addrs...]
    program_.operations.push_back(static_cast<uint16_t>(node.type_id));

    // Encode input addresses
    const size_t inputs_at = program_.operations.size();
    for (size_t i = 0; i < node.inputs.size(); ++i) {
      const ProgramGraph::Port& input = node.inputs[i];
      uint16_t addr;

      if (input.connections.empty()) {
        // Use default value as constant
        addr = mem.AddConstant(node.default_inputs[i]);
      } else {
        // Connected: lookup source address
        const ProgramGraph::PortLocation& src = input.connections[0];
        uint64_t key = MakePortKey(src.node_id, src.port_index);
        auto it = port_to_address.find(key);
        assert(it != port_to_address.end());
        addr = it->second;
      }

      program_.operations.push_back(addr);
      program_.buffer_specs[addr].users_count++;
    }

    // Allocate and encode output addresses
    for (size_t i = 0; i < node.outputs.size(); ++i) {
      uint16_t addr = mem.AddVar();
      port_to_address[MakePortKey(node_id, static_cast<uint32_t>(i))] = addr;
      program_.operations.push_back(addr);
    }

    // Compile params into operations array
    const size_t params_at = program_.operations.size();
    if (type.compile_func != nullptr) {
      CompileContext ctx(program_.operations, program_.heap_resources,
                         node.params);
      type.compile_func(ctx);

      if (ctx.HasError()) {
        return CompilationResult::MakeError(ctx.GetErrorMessage().c_str(),
                                            node.id);
      }
    }
    if (program_.operations.size() == params_at) {
      program_.operations.push_back(0);  // no params
    }

    // Track output nodes
    if (type.category == Category::OUTPUT) {
      if (program_.outputs_count == MAX_OUTPUTS) {
        return CompilationResult::MakeError("Too many output nodes", node.id);
      }

      OutputInfo& out_info = program_.outputs[program_.outputs_count];
      out_info.node_id = node_id;

      // Output reads its first input: linked, or its default value
      assert(!node.inputs.empty());
      out_info.buffer_address = program_.operations[inputs_at];
      program_.outputs_count++;
    }
  }

  program_.buffer_count = mem.next_address;

  return CompilationResult::MakeSuccess();
}

void GraphRuntime::PrepareState(State& state, unsigned int buffer_size) const {
  state.buffer_size_ = buffer_size;

  // Resize buffers array
  state.buffers_.resize(program_.buffer_count);

  // Allocate buffer data
  // ? For now, allocate one float per buffer for single-point queries
  if (state.buffer_datas_.size() < program_.buffer_count) {
    state.buffer_datas_.resize(program_.buffer_count);
  }

  for (size_t i = 0; i < program_.buffer_specs.size(); ++i) {
    const BufferSpec& spec = program_.buffer_specs[i];
    Buffer& buf = state.buffers_[spec.address];
    BufferData& data = state.buffer_datas_[i];

    if (spec.is_constant) {
      buf.is_constant = true;
      buf.constant_value = spec.constant_value;
      // Constant buffers need data pointer for uniform access
      if (data.capacity < buffer_size) {
        delete[] data.data;
        data.data = new float[buffer_size];
        data.capacity = buffer_size;
      }
      std::fill(data.data, data.data + buffer_size, spec.constant_value);
      buf.data = data.data;
    } else if (spec.is_binding) {
      buf.is_binding = true;
      buf.is_constant = false;
      // Bindings are set externally
      if (data.capacity < buffer_size) {
        delete[] data.data;
        data.data = new float[buffer_size];
        data.capacity = buffer_size;
      }
      buf.data = data.data;
    } else {
      buf.is_constant = false;
      buf.is_binding = false;
      if (data.capacity < buffer_size) {
        delete[] data.data;
        data.data = new float[buffer_size];
        data.capacity = buffer_size;
      }
      buf.data = data.data;
    }
  }
}

void GraphRuntime::GenerateSingle(State& state, glm::vec2 position,
                                  Sample& out_sample) const {
  const NodeTypeDB& type_db = NodeTypeDB::Instance();

  // Set input bindings by what they hold
  for (const InputInfo& input : program_.inputs) {
    state.buffers_[input.buffer_address].data[0] =
        input.type_id == static_cast<uint32_t>(NodeTypeID::InputY)
            ? position.y
            : position.x;
  }

  // Execute operations using the execution map
  std::span<const uint16_t> operations(program_.operations);

  for (const auto& op_info : program_.default_execution_map.operations) {
    unsigned int pc = op_info.address;

    const uint16_t type_id = operations[pc++];
    const NodeType& type = type_db.Get(static_cast<NodeTypeID>(type_id));

    const uint32_t inputs_count = type.inputs.size();
    const uint32_t outputs_count = type.outputs.size();

    // Read input addresses from bytecode
    std::vector<uint32_t> input_addresses(inputs_count);
    for (uint32_t i = 0; i < inputs_count; ++i) {
      input_addresses[i] = operations[pc++];
    }

    // Read output addresses from bytecode
    std::vector<uint32_t> output_addresses(outputs_count);
    for (uint32_t i = 0; i < outputs_count; ++i) {
      output_addresses[i] = operations[pc++];
    }

    // Read params from bytecode
    std::span<const uint8_t> params = ReadParams(operations, pc);

    if (type.process_buffer_func != nullptr) {
      ProcessContext ctx(input_addresses, output_addresses, params,
                         state.buffers_, state.resources);
      type.process_buffer_func(ctx);
    }
  }

  // Extract output from the first output node
  if (program_.outputs_count > 0) {
    uint16_t output_addr = program_.outputs[0].buffer_address;
    if (output_addr < state.buffers_.size()) {
      out_sample.height = state.buffers_[output_addr].data[0];
    }
  }
}

}  // namespace PCG