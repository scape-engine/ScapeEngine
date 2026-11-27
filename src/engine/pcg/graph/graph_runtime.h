#ifndef GRAPH_RUNTIME_H
#define GRAPH_RUNTIME_H

#include <array>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

#include "engine/pcg/core/sample.h"
#include "program_graph.h"

/*******************************************************************************
 * GRAPH RUNTIME
 * TODO:
 * - Add proper output tracking (currently assumes "last buffer")
 * - Add batch processing
 * - Enhance input type mapping (currrently assumes InputX=0, InputY=1)
 ******************************************************************************/

namespace PCG {

struct CompilationResult {
  bool success = false;
  int node_id = -1;
  std::string message;

  static CompilationResult MakeSuccess() {
    CompilationResult result;
    result.success = true;
    return result;
  }

  static CompilationResult MakeError(const char* message, int node_id = -1) {
    CompilationResult result;
    result.success = false;
    result.node_id = node_id;
    result.message = message;
    return result;
  }
};

class GraphRuntime {
public:
  static const unsigned int MAX_OUTPUTS = 24;
  // static const unsigned int MAX_INPUTS = 8;

  // Owns float data for one buffer slot
  struct BufferData {
    float* data = nullptr;
    unsigned int capacity = 0;
  };

  // Consists of values of a node output
  struct Buffer {
    float* data = nullptr;
    float constant_value = 0.0f;
    bool is_constant = false;
    bool is_binding = false;
  };

  struct BufferSpec {
    uint16_t address = 0;
    uint16_t data_index = 0;
    uint16_t users_count = 0;
    float constant_value = 0;
    bool is_constant = false;
    bool is_binding = false;
  };

  // Contains a list of adresses to the operations to exectute in a query
  struct ExecutionMap {
    struct OperationInfo {
      uint16_t address = 0;
    };

    std::vector<OperationInfo> operations;

    void Clear() { operations.clear(); }
  };

  struct InputInfo {
    // ? The buffer are allocated by the user and is mapped temporarily
    unsigned int buffer_address = 0;
    uint32_t type_id = 0;  // the input node: what the binding holds
  };

  struct OutputInfo {
    unsigned int buffer_address;
    unsigned int dependency_graph_node_index;
    unsigned int node_id;
  };

  // Contains the data the program will modify while it runs
  // TODO: same state should be re-used with multiple programs
  class State {
  public:
    ~State() { Clear(); }

    const Buffer& GetBuffer(uint16_t address) const {
      return buffers_[address];
    }
    uint32_t GetBufferSize() const { return buffer_size_; }

    void Clear();

    // Resource cache: expensive objects (e.g. FastNoise2 instances) persist
    // here across Execute() calls. Owned by State, not by ProcessContext.
    std::unordered_map<std::string, std::shared_ptr<void>> resources;

  private:
    friend class GraphRuntime;

    std::vector<Buffer> buffers_;
    std::vector<BufferData> buffer_datas_;
    unsigned int buffer_size_ = 0;
    unsigned int buffer_capacity_ = 0;
  };

  GraphRuntime();
  ~GraphRuntime();

  void Clear();
  CompilationResult Compile(const ProgramGraph& graph);

  void PrepareState(State& state, unsigned int buffer_size) const;
  void GenerateSingle(State& state, glm::vec2 position,
                      Sample& out_sample) const;

  struct HeapResource {
    void* ptr;
    void (*deleter)(void*);

    void Free() {
      if (deleter && ptr) {
        deleter(ptr);
        ptr = nullptr;
      }
    }
  };

  static inline std::span<const uint8_t> ReadParams(
      std::span<const uint16_t> operations, unsigned int& pc) {
    // As CompileContext::SetParams writes them
    const uint16_t params_size_in_words = operations[pc++];
    if (params_size_in_words == 0) {
      return {};
    }
    pc += operations[pc];  // to the aligned params
    const uint8_t* params_ptr =
        reinterpret_cast<const uint8_t*>(&operations[pc]);
    pc += params_size_in_words;
    return {params_ptr, params_size_in_words * sizeof(uint16_t)};
  }

  // Holds buffer address lists and params
  // Constructed on the stack once per node inside the execution loop.
  class _ProcessContext {
  public:
    _ProcessContext(const std::vector<uint32_t>& input_addresses,
                    const std::vector<uint32_t>& output_addresses,
                    std::span<const uint8_t> params)
        : input_addresses_(input_addresses),
          output_addresses_(output_addresses),
          params_(params) {}

    template <typename T>
    const T& GetParams() const {
      assert(sizeof(T) <= params_.size());
      return *reinterpret_cast<const T*>(params_.data());
    }

    uint32_t GetInputAddress(uint32_t i) const { return input_addresses_[i]; }

  protected:
    uint32_t GetOutputAddress(uint32_t i) const { return output_addresses_[i]; }

  private:
    const std::vector<uint32_t>& input_addresses_;
    const std::vector<uint32_t>& output_addresses_;
    std::span<const uint8_t> params_;
  };

  // Adds buffer read/write and resource access
  // Includes functions usable by node implementation
  class ProcessContext : public _ProcessContext {
  public:
    ProcessContext(
        const std::vector<uint32_t>& input_addresses,
        const std::vector<uint32_t>& output_addresses,
        std::span<const uint8_t> params, std::vector<Buffer>& buffers,
        std::unordered_map<std::string, std::shared_ptr<void>>& resources)
        : _ProcessContext(input_addresses, output_addresses, params),
          buffers_(buffers),
          resources_(resources) {}

    float GetInput(uint32_t i) const {
      return buffers_[GetInputAddress(i)].data[0];
    }

    void SetOutput(uint32_t i, float value) {
      buffers_[GetOutputAddress(i)].data[0] = value;
    }

    template <typename T, typename... Args>
    T* GetOrCreateResource(const std::string& key, Args&&... args) {
      auto it = resources_.find(key);
      if (it != resources_.end())
        return static_cast<T*>(it->second.get());
      auto pointer = std::make_shared<T>(std::forward<Args>(args)...);
      T* raw = pointer.get();
      resources_[key] = pointer;
      return raw;
    }

  private:
    std::vector<Buffer>& buffers_;
    std::unordered_map<std::string, std::shared_ptr<void>>& resources_;
  };

  // Functions usable by node implementations during range analysis
  // ! This is a stub for now
  class RangeAnalysisContext : public _ProcessContext {
  public:
    using _ProcessContext::_ProcessContext;
  };

  // Function pointer types
  typedef void (*ProcessBufferFunc)(ProcessContext&);
  typedef void (*RangeAnalysisFunc)(RangeAnalysisContext&);

private:
  // Compiled program data
  // TODO: should remain read-only and constant after compilation
  struct Program {
    std::vector<uint16_t> operations;
    std::vector<BufferSpec> buffer_specs;
    std::vector<HeapResource> heap_resources;
    ExecutionMap default_execution_map;

    std::vector<InputInfo> inputs;
    std::array<OutputInfo, MAX_OUTPUTS> outputs;
    unsigned int outputs_count = 0;

    unsigned int buffer_count = 0;

    void Clear() {
      operations.clear();
      buffer_specs.clear();
      inputs.clear();
      default_execution_map.Clear();
      outputs_count = 0;

      for (HeapResource& hr : heap_resources) {
        hr.Free();
      }
      heap_resources.clear();
      buffer_count = 0;
    }
  };

  Program program_;
};

}  // namespace PCG

#endif  // GRAPH_RUNTIME_H