#ifndef NODE_TYPES_H
#define NODE_TYPES_H

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "graph_compiler.h"
#include "graph_runtime.h"

namespace PCG {
//
// NODE TYPE DEFINITIONS
//
enum class Category {
  INPUT,
  OUTPUT,
  MATH,
  CONVERT,  // godot_voxel's: clamping, remapping, snapping
  NOISE,
  FILTER,
  EROSION,
  UTILITY,
  COUNT  // ! check if this should be here
};

struct NodeType {
  struct Port {
    std::string name;
    float default_value = 0.0f;

    // For auto-connecting unconnected inputs to X/Y/Z
    // TODO: AutoConnect auto_connect = AutoConnect::None;
    Port(std::string p_name, float p_default = 0.0f)
        : name(std::move(p_name)), default_value(p_default) {}
  };

  struct Param {
    enum class Type { Float, Int, Enum, Bool };
    std::string name;
    Type type;
    std::variant<float, int, bool, std::string> default_value;
    float min_value = 0.0f;
    float max_value = 1.0f;
    bool has_range = false;
    std::optional<uint32_t> index;  // uint32_t index = -1;
    std::vector<std::string> enum_items;
  };

  std::string name;
  Category category;
  std::vector<Port> inputs;
  std::vector<Port> outputs;
  std::vector<Param> params;

  // Lookup maps - populated by NodeTypeDB after registration
  std::unordered_map<std::string, uint32_t> param_name_to_index;
  std::unordered_map<std::string, uint32_t> input_name_to_index;

  // Processing - assigned separately from registration
  CompileFunc compile_func = nullptr;
  GraphRuntime::ProcessBufferFunc process_buffer_func = nullptr;
  GraphRuntime::RangeAnalysisFunc range_analysis_func = nullptr;
};

//
// NODE TYPE IDS - terrain generation nodes
//

// TODO: revise the list
enum class NodeTypeID : uint32_t {
  // Inputs
  InputX = 0,
  InputY,
  InputZ,
  Constant,

  // Outputs
  OutputHeight,
  OutputSlope,

  // Math operations
  Add,
  Subtract,
  Multiply,
  Divide,
  Clamp,
  Mix,  // Lerp
  Abs,
  Power,
  Min,
  Max,
  Sin,
  Floor,
  Sqrt,
  Fract,
  Wrap,
  PowerInt,  // godot_voxel's Powi
  Stepify,
  ClampConstant,  // godot_voxel's ClampC: bounds as params
  Remap,
  Smoothstep,

  // Noise generators
  OpenSimplex2,        // OpenSimplex2S
  OpenSimplex2_Deriv,  // analytical derivatives
  UberFBM,             // custom fBm with erosion

  // Noise generators - FastNoise2
  FN2_Simplex2D,
  FN2_Simplex3D,
  FN2_Perlin2D,
  FN2_Perlin3D,
  FN2_Cellular2D,
  FN2_Value2D,

  // Domain operations
  DomainWarp,
  DomainWarpFractal,  // Progressive fractal warp

  // Filters - operators
  Ridge,    // Ridge::Blend
  Terrace,  // Remap::Terrace
  Plateau,  // Remap::Plateau

  // Erosion - erosion system
  SlopeErosion,
  RidgeErosion,
  AltitudeErosion,

  // Utility
  Preview,  // SDF preview node
  Comment,

  COUNT
};

//
// NODE TYPE DATABASE
//
class NodeTypeDB {
public:
  static NodeTypeDB& Instance();

  int GetTypeCount() const { return static_cast<int>(_types.size()); }
  bool IsValidTypeId(int type_id) const {
    return type_id >= 0 && type_id < static_cast<int>(_types.size());
  }

  const NodeType& Get(NodeTypeID id) const;
  const NodeType& Get(uint32_t id) const;

  // Lookup by name - serialization
  bool TryGetId(const std::string& name, NodeTypeID& out_id) const;
  bool TryGetParamIndex(uint32_t type_id, const std::string& name,
                        uint32_t& out_index) const;
  bool TryGetInputIndex(uint32_t type_id, const std::string& name,
                        uint32_t& out_index) const;
  bool TryGetOutputIndex(uint32_t type_id, const std::string& name,
                         uint32_t& out_index) const;

  void ForEachType(
      const std::function<void(uint32_t, const NodeType&)>& func) const;

private:
  NodeTypeDB();
  void RegisterAllNodes();
  void BuildLookupIndices();

  std::array<NodeType, static_cast<size_t>(NodeTypeID::COUNT)> _types;
  std::unordered_map<std::string, NodeTypeID> _name_to_id;
};

}  // namespace PCG

#endif  // NODE_TYPES_H