#include "node_types.h"

#include "nodes/curves.h"
#include "nodes/inputs.h"
#include "nodes/math_funcs.h"
#include "nodes/math_ops.h"
#include "nodes/noise.h"
#include "nodes/outputs.h"
#include "nodes/sdf.h"

/*******************************************************************************
 * NODE TYPES
 * Serves as a registry for node definitions
 * Includes the inputs/outputs/parameters for node type
 ******************************************************************************/
namespace PCG {

NodeTypeDB& NodeTypeDB::Instance() {
  static NodeTypeDB db;
  return db;
}

NodeTypeDB::NodeTypeDB() {
  RegisterAllNodes();
  BuildLookupIndices();
}

const NodeType& NodeTypeDB::Get(NodeTypeID id) const {
  return _types[static_cast<size_t>(id)];
}

const NodeType& NodeTypeDB::Get(uint32_t id) const {
  return _types[id];
}

bool NodeTypeDB::TryGetId(const std::string& name, NodeTypeID& out_id) const {
  auto it = _name_to_id.find(name);
  if (it == _name_to_id.end()) {
    return false;
  }
  out_id = it->second;
  return true;
}

void NodeTypeDB::ForEachType(
    const std::function<void(uint32_t, const NodeType&)>& func) const {
  for (size_t i = 0; i < _types.size(); ++i) {
    if (!_types[i].name.empty()) {
      func(static_cast<uint32_t>(i), _types[i]);
    }
  }
}

void NodeTypeDB::BuildLookupIndices() {
  for (size_t i = 0; i < _types.size(); ++i) {
    NodeType& t = _types[i];

    if (t.name.empty()) {
      continue;
    }

    // Name -> ID lookup
    _name_to_id[t.name] = static_cast<NodeTypeID>(i);

    // Param name -> index lookup + set param indices
    for (uint32_t param_idx = 0; param_idx < t.params.size(); ++param_idx) {
      NodeType::Param& p = t.params[param_idx];
      t.param_name_to_index[p.name] = param_idx;
      p.index = param_idx;
    }

    // Input name -> index lookup
    for (uint32_t input_idx = 0; input_idx < t.inputs.size(); ++input_idx) {
      t.input_name_to_index[t.inputs[input_idx].name] = input_idx;
    }
  }
}

bool NodeTypeDB::TryGetParamIndex(uint32_t type_id, const std::string& name,
                                  uint32_t& out_index) const {
  if (type_id >= _types.size()) {
    return false;
  }
  const NodeType& t = _types[type_id];
  auto it = t.param_name_to_index.find(name);
  if (it == t.param_name_to_index.end()) {
    return false;
  }
  out_index = it->second;
  return true;
}

bool NodeTypeDB::TryGetInputIndex(uint32_t type_id, const std::string& name,
                                  uint32_t& out_index) const {
  if (type_id >= _types.size()) {
    return false;
  }
  const NodeType& t = _types[type_id];
  auto it = t.input_name_to_index.find(name);
  if (it == t.input_name_to_index.end()) {
    return false;
  }
  out_index = it->second;
  return true;
}

bool NodeTypeDB::TryGetOutputIndex(uint32_t type_id, const std::string& name,
                                   uint32_t& out_index) const {
  if (type_id >= _types.size()) {
    return false;
  }
  const NodeType& t = _types[type_id];

  // use linear search
  for (uint32_t i = 0; i < t.outputs.size(); ++i) {
    if (t.outputs[i].name == name) {
      out_index = i;
      return true;
    }
  }
  return false;
}

// ? We need to check where processing functions should be assigned
// e.g. ProcessContext
void NodeTypeDB::RegisterAllNodes() {

  std::span<NodeType> types(_types);

  RegisterInputNodes(types);
  RegisterOutputNodes(types);
  RegisterMathOpsNodes(types);
  RegisterMathFuncsNodes(types);
  RegisterNoiseNodes(types);
  // RegisterSdfNodes();
}

}  // namespace PCG