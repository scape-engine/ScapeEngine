#ifndef MODEL_DESCRIPTOR_H
#define MODEL_DESCRIPTOR_H

#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/animation/gait_set.h"
#include "engine/animation/pose_set.h"
#include "engine/pcg/pipeline/pipeline_descriptor.h"

namespace ProcModel {

struct PartDescriptor {
  std::string id;
  std::string name;
  float weight;
  // Parts sharing a key are placed once per model: a group offering a key
  // already placed is skipped. Empty: none
  std::string key;
};

struct LocatorOperation {
  std::string target_group_id;  // empty = all groups
  PCG::PipelineEntry entry;
};

struct SelectionGroup {
  std::string group_id;
  std::vector<PartDescriptor> parts;
  bool required = true;
  std::vector<std::string> parent;  // Empty = root group

  // Under a part, the groups of one list are picked together from one seed;
  // its lists are picked in order, each from its own seed (root groups are
  // one list)
  int list = 0;

  // transform nodes; if unset = variant-only
  std::optional<std::vector<std::string>> locators;

  // If true: each attach point in locators re-runs WeightedSelect, allowing
  // different parts at different attachments (organic variation - branches,
  // leaves). If false (default): one part is selected for the group and
  // duplicated to every attachment (uniform assembly — columns, pillars).
  bool unique_per_locator = false;

  // Scale inherited multiplicatively from this group's activator. A value of
  // 0.6 means: parts in this group are 60% the size of their parent's scale.
  // Combined with parent's already-scaled value: part attached to parent
  // (scale 0.6) of a BASE (scale 1.0) end up at 0.6 × 0.6 = 0.36 of root.
  float scale_factor = 1.0f;
  std::optional<float> scale_jitter;  // ± random perturbation

  // How many of the group's locator placements stay, drawn between the two
  // (worldgen's node distribution); unset keeps every one
  std::optional<glm::ivec2> keep;
};

//
// TRANSFORM RANGE
// used for per-part transforms
// TODO: revise whether this is redundant, if not, then rename
struct TransformRange {
  std::string part_id;

  std::optional<glm::vec3> scale_min;
  std::optional<glm::vec3> scale_max;
  std::optional<glm::vec3> rotation_min;
  std::optional<glm::vec3> rotation_max;
};

struct DeformationRange {
  std::string group_id;

  std::optional<float> taper_factor_min;
  std::optional<float> taper_factor_max;

  std::optional<float> twist_angle_min;
  std::optional<float> twist_angle_max;

  std::optional<float> bend_angle_min;
  std::optional<float> bend_angle_max;

  std::optional<float> noise_amplitude_min;
  std::optional<float> noise_amplitude_max;

  // Ridges' depth, a fraction of the radius
  std::optional<float> ridge_amplitude_min;
  std::optional<float> ridge_amplitude_max;

  // How far the centre line wanders sideways, in mesh units
  std::optional<float> wander_amplitude_min;
  std::optional<float> wander_amplitude_max;
};

struct ConstraintRule {
  enum class Type { EXCLUDES, REQUIRES };

  std::string id;
  Type type;
  std::string part_a;
  std::string part_b;
};

struct ParameterBinding {
  std::string source_part;
  std::string source_param;
  std::string target_part;
  std::string target_param;
  float ratio = 1.0f;
};

// Full archetype definition
struct ModelDescriptor {
  std::string model_id;  // archetype identity
  std::string model_name;
  std::string path;
  std::string domain;  // e.g. vegetation, building, etc

  std::vector<SelectionGroup> selection_groups;
  std::vector<TransformRange> transform_ranges;
  std::vector<ConstraintRule> constraints;
  std::vector<ParameterBinding> parameter_bindings;
  std::vector<DeformationRange> part_deformation_ranges;
  std::optional<DeformationRange> model_deformation_range;

  // Archetype-level attributes
  std::optional<glm::vec3> scale_min;  // model scale range
  std::optional<glm::vec3> scale_max;
  std::vector<std::string> tags;  // ex: "vegetation", "tropical"

  // Groups activated by parts of their own chain (branches on branches) nest
  // at most this many levels below the root groups
  int max_depth = 6;
  // How parts are picked (worldgen's walk): unset, one stream for the whole
  // model; set, two streams, the second taking over a pick with a chance
  // growing by this much per level
  std::optional<float> distortion;

  // Procedural textures by material name: each names a worldgen texture
  // description (relative to the descriptor), coloured by the planet
  std::unordered_map<std::string, std::string> textures;

  // Locator modifiers: declarative transformations applied per-group
  // to each locator's world frame before a part is placed.
  // Authored as a list of (kind, params) entries.
  std::vector<LocatorOperation> locator_operations;

  // Seeded proportions of an animated model: poses of one clip, drawn
  // together per instance
  std::optional<PoseSet> poses;

  // An animated model's gaits, which herds move it in ("movement")
  std::optional<GaitSet> gaits;
};

}  // namespace ProcModel

#endif  // MODEL_DESCRIPTOR_H
