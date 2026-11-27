#ifndef GRAPH_COMPILER_H
#define GRAPH_COMPILER_H

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "graph_runtime.h"
#include "program_graph.h"

/*******************************************************************************
 * GRAPH COMPILER
 * TODO:
 * - Add expression node expansion (e.g. math expressions)
 * - Add function node expansion (e.g. nodes that embed other premade graphs)
 * - Add rely nodes (needed for function expansion)
 * - Add combining inputs (Multiple Input nodes in graph merged into a binding)
 * - add node simplification?
 * - add outer group optimization (XY caching), needed for 3D terrain
 * - Add dependency graph for range analysis
 * Reference:
 * https://github.com/Zylann/godot_voxel/blob/master/generators/graph/voxel_graph_compiler.cpp
 ******************************************************************************/

namespace PCG {

class NodeTypeDB;

struct PortRemap {
  ProgramGraph::PortLocation original;
  ProgramGraph::PortLocation expanded;
};

struct ExpandedNodeRemap {
  uint32_t expanded_node_id;
  uint32_t original_node_id;
};

struct GraphRemappingInfo {
  std::vector<PortRemap> user_to_expanded_ports;
  std::vector<ExpandedNodeRemap> expanded_to_user_node_ids;
};

// Preprocess graph and apply optimizations before main compilation pass
CompilationResult ExpandGraph(const ProgramGraph& graph,
                              ProgramGraph& expanded_graph,
                              const NodeTypeDB& type_db,
                              GraphRemappingInfo* remap_info, bool debug);

// Context is passed to a node's compile_func, if it has one
// Allows nodes to preprocess and store parameters at compile time
class CompileContext {
public:
  CompileContext(
      std::vector<uint16_t>& program,
      std::vector<GraphRuntime::HeapResource>& heap_resources,
      const std::vector<std::variant<float, int, bool, std::string>>& params)
      : program_(program), heap_resources_(heap_resources), params_(params) {}

  const std::variant<float, int, bool, std::string>& GetParam(size_t i) const {
    assert(i < params_.size());
    return params_[i];
  }

  template <typename T>
  void SetParams(T params) {
    static_assert(std::is_standard_layout_v<T> == true);
    static_assert(std::is_trivial_v<T> == true);

    assert(!params_added_);

    const size_t params_alignment = std::max(alignof(T), alignof(uint16_t));
    const size_t size_index = program_.size();

    // [size in words, words to the aligned params, padding..., params],
    // read back by GraphRuntime::ReadParams
    program_.push_back(0);
    program_.push_back(0);

    const size_t struct_offset =
        AlignUp(program_.size() * sizeof(uint16_t), params_alignment) /
        sizeof(uint16_t);

    params_size_in_words_ =
        (sizeof(T) + sizeof(uint16_t) - 1) / sizeof(uint16_t);
    program_[size_index] = static_cast<uint16_t>(params_size_in_words_);
    program_[size_index + 1] =
        static_cast<uint16_t>(struct_offset - (size_index + 1));
    program_.resize(struct_offset + params_size_in_words_);

    T& p = *reinterpret_cast<T*>(&program_[struct_offset]);
    p = params;

    params_added_ = true;
  }

  // Required if compilation step produces a resource to be deleted
  template <typename T>
  void AddDeleteCleanup(T* ptr) {
    GraphRuntime::HeapResource hr;
    hr.ptr = ptr;
    hr.deleter = [](void* p) {
      delete reinterpret_cast<T*>(p);
    };
    heap_resources_.push_back(hr);
  }

  void MakeError(const std::string& message) {
    error_message_ = message;
    has_error_ = true;
  }

  bool HasError() const { return has_error_; }

  const std::string& GetErrorMessage() const { return error_message_; }

  size_t GetParamsSizeInWords() const { return params_size_in_words_; }

private:
  // ! Helper function
  static size_t AlignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
  }

  std::vector<uint16_t>& program_;
  const std::vector<std::variant<float, int, bool, std::string>>& params_;
  std::vector<GraphRuntime::HeapResource>& heap_resources_;
  std::string error_message_;
  size_t params_size_in_words_ = 0;
  bool has_error_ = false;
  bool params_added_ = false;
};

typedef void (*CompileFunc)(CompileContext&);

}  // namespace PCG

#endif  // GRAPH_COMPILER_H