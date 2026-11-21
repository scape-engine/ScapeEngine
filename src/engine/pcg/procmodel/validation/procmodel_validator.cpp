#include "procmodel_validator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "engine/core/logger.h"
#include "engine/core/types/geometry_data.h"

namespace ProcModel {

namespace {
// Descriptor IDs follow the format:
// part_id>[_at_<locator>][_under_<activator_instance>]
// where the _under_ suffix may itself contain _at_/_under_
// from nested activators.
struct ParsedDescriptorId {
  std::string part_id;
  std::string locator;  // empty if variant-only
};

ParsedDescriptorId ParseDescriptorId(const std::string& id) {
  ParsedDescriptorId out;
  size_t at = id.find("_at_");
  if (at == std::string::npos) {
    out.part_id = id;
    return out;
  }
  out.part_id = id.substr(0, at);
  size_t locator_start = at + 4;
  size_t under = id.find("_under_", locator_start);
  out.locator = (under == std::string::npos)
                    ? id.substr(locator_start)
                    : id.substr(locator_start, under - locator_start);
  return out;
}

}  // namespace

// Forward axis convention (engine-wide): +Z in local space.
// Transformed by the node's world_transform to produce world-space forward.
static constexpr glm::vec3 kLocalForward = glm::vec3(0.0f, 0.0f, 1.0f);

// Threshold (in radians) beyond which attachment-point forward axes within
// a group are considered misaligned. ~20 degrees.
static constexpr float kForwardAlignmentThresholdRad = 0.35f;

//
// Helpers
//
static int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch())
      .count();
}

static void ExpandAABB(AABB& box, const glm::vec3& p) {
  if (!box.valid) {
    box.min = p;
    box.max = p;
    box.valid = true;
    return;
  }
  box.min = glm::min(box.min, p);
  box.max = glm::max(box.max, p);
}

static void MergeAABB(AABB& into, const AABB& other) {
  if (!other.valid)
    return;
  if (!into.valid) {
    into = other;
    return;
  }
  into.min = glm::min(into.min, other.min);
  into.max = glm::max(into.max, other.max);
}

// Compute an AABB for a single mesh under a given world transform by
// iterating its CPU-side vertex positions.
// TODO: cache per-mesh local AABBs on ModelGraph to avoid recomputation
// across validation calls.
static AABB ComputeMeshAABB(const Geometry::MeshData& mesh,
                            const glm::mat4& world_transform) {
  AABB box;
  for (const auto& v : mesh.vertices) {
    glm::vec4 p = world_transform * glm::vec4(v.position, 1.0f);
    ExpandAABB(box, glm::vec3(p));
  }
  return box;
}

//
// CHECK: constraint re-verification
//    Catches solver bugs and confirms final selection satisfies all rules.
//
static void CheckConstraints(const InstanceModel& result,
                             const ModelDescriptor& descriptor,
                             ProcModelSample& out) {
  std::unordered_set<std::string> selected;
  for (const auto& d : result.descriptors) {
    auto parsed = ParseDescriptorId(d.descriptor_id);
    selected.insert(parsed.part_id);
  }

  for (const auto& rule : descriptor.constraints) {
    const bool a = selected.count(rule.part_a) > 0;
    const bool b = selected.count(rule.part_b) > 0;

    switch (rule.type) {
      case ConstraintRule::Type::EXCLUDES:
        if (a && b) {
          Diagnostic d;
          d.code = "constraint.excludes";
          d.severity = Diagnostic::Severity::Error;
          d.part_ids = {rule.part_a, rule.part_b};
          d.message = "Mutually-excluded parts both selected";
          out.diagnostics.push_back(std::move(d));
        }
        break;
      case ConstraintRule::Type::REQUIRES:
        if (a != b) {
          Diagnostic d;
          d.code = "constraint.requires";
          d.severity = Diagnostic::Severity::Error;
          d.part_ids = {rule.part_a, rule.part_b};
          d.message = "REQUIRES rule unsatisfied";
          out.diagnostics.push_back(std::move(d));
        }
        break;
    }
  }
}

//
// CHECK: attachment uniqueness + dangling attach_to references
//    Duplicate attachment at the same node is a hard structural bug.
//    Dangling attach_to means the group references an attachment node that
//    doesn't exist in the graph.
//
static void CheckLocators(const InstanceModel& result, const ModelGraph& graph,
                          ProcModelSample& out) {
  std::unordered_map<std::string, std::vector<std::string>> locator_occupants;

  for (const auto& d : result.descriptors) {
    auto parsed = ParseDescriptorId(d.descriptor_id);
    if (parsed.locator.empty())
      continue;

    if (graph.node_lookup.find(parsed.locator) == graph.node_lookup.end()) {
      Diagnostic diag;
      diag.code = "locator.dangling";
      diag.severity = Diagnostic::Severity::Error;
      diag.part_ids = {parsed.part_id};
      diag.message = "Locator references non-existent node: " + parsed.locator;
      out.diagnostics.push_back(std::move(diag));
      continue;
    }

    // Scope uniqueness to the activator instance — same locator name is
    // legitimately reused across different parent instances (e.g. leaves
    // on different branches share locator node names by GLTF construction).
    std::string occupant_key = d.activator_id + "::" + parsed.locator;
    locator_occupants[occupant_key].push_back(parsed.part_id);
  }

  for (const auto& [key, occupants] : locator_occupants) {
    if (occupants.size() > 1) {
      Diagnostic diag;
      diag.code = "locator.duplicate";
      diag.severity = Diagnostic::Severity::Error;
      diag.part_ids = occupants;
      diag.message = "Multiple parts resolved to locator '" + key +
                     "' under same activator";
      out.diagnostics.push_back(std::move(diag));
    }
  }
}

//
// CHECK: required groups activated
//    Any group marked required must have contributed at least one selection.
//
static void CheckGroupActivation(const InstanceModel& result,
                                 const ModelDescriptor& descriptor,
                                 ProcModelSample& out) {
  std::unordered_set<std::string> active_groups;
  for (const auto& d : result.descriptors) {
    active_groups.insert(d.group_id);
  }
  for (const std::string& g : active_groups) {
    out.active_group_ids.push_back(g);
  }

  // Track which groups *could* have activated. A required group whose
  // parent part was never selected is not a violation — its
  // activation is conditional by design.
  std::unordered_set<std::string> selected_parts;
  for (const auto& d : result.descriptors) {
    auto parsed = ParseDescriptorId(d.descriptor_id);
    selected_parts.insert(parsed.part_id);
  }

  for (const auto& group : descriptor.selection_groups) {
    if (!group.required)
      continue;

    const bool is_root = group.parent.empty();

    // A group can activate if any of its parent parts was selected
    bool activator_selected = false;
    for (const auto& parent_id : group.parent) {
      if (selected_parts.count(parent_id) > 0) {
        activator_selected = true;
        break;
      }
    }

    // Only flag required groups that *should* have activated but didn't
    if (!is_root && !activator_selected)
      continue;

    if (active_groups.count(group.group_id) == 0) {
      Diagnostic diag;
      diag.code = "group.missing_activation";
      diag.severity = Diagnostic::Severity::Error;
      diag.group_id = group.group_id;
      diag.message = "Required group produced no selection";
      out.diagnostics.push_back(std::move(diag));
    }
  }
}

//
// CHECK: forward axis consistency within a group
//    For each selection group, collect the world-space forward vectors of
//    its locators; warn if any pair diverges beyond threshold.
//    Engine convention: +Z is forward (left-handed).
//
static void CheckForwardAxisConsistency(const ModelGraph& graph,
                                        const ModelDescriptor& descriptor,
                                        ProcModelSample& out) {
  for (const auto& group : descriptor.selection_groups) {
    // ! checks locators exposed by parts in group rather than
    // ! locators the group fills
    // TODO: refactor code to walk activator relationships
    // for better misalignment detection

    // Collect locators from the group's own locator list
    std::vector<std::string> group_locators;
    if (group.locators) {
      group_locators = *group.locators;
    }
    if (group_locators.size() < 2)
      continue;

    std::vector<glm::vec3> forwards;
    forwards.reserve(group_locators.size());

    for (const std::string& locator_id : group_locators) {
      auto it = graph.node_lookup.find(locator_id);
      if (it == graph.node_lookup.end())
        continue;

      glm::vec4 fwd =
          it->second->world_transform * glm::vec4(kLocalForward, 0.0f);
      glm::vec3 f(fwd);
      float len = glm::length(f);
      if (len < 1e-6f)
        continue;
      forwards.push_back(f / len);
    }

    if (forwards.size() < 2)
      continue;

    const glm::vec3& ref = forwards[0];
    for (size_t i = 1; i < forwards.size(); ++i) {
      float dot = glm::clamp(glm::dot(ref, forwards[i]), -1.0f, 1.0f);
      float angle = std::acos(dot);
      if (angle > kForwardAlignmentThresholdRad) {
        Diagnostic diag;
        diag.code = "group.forward_misaligned";
        diag.severity = Diagnostic::Severity::Warning;
        diag.group_id = group.group_id;
        diag.message = "Locator forward axes diverge within group";
        out.diagnostics.push_back(std::move(diag));
        break;
      }
    }
  }
}

//
// CHECK: compute geometric bounds
//    Fills per-part, per-group, and full-model AABBs. Accumulate
//    per authored part
//
static void ComputeBounds(const InstanceData& result, const ModelGraph& graph,
                          ProcModelSample& out) {
  // Whole-instance scale lives on the root entity; descendants inherit it.
  const glm::mat4 M_inst =
      glm::scale(glm::mat4(1.0f), result.model.model_scale);

  for (const auto& d : result.model.descriptors) {
    AABB part_box;

    // Decompose local_transform the same way ModelInstantiator does, then
    // re-compose with applied_rotation as a quaternion multiplied onto the
    // base rotation. applied_scale is intentionally NOT used here because
    // ModelInstantiator doesn't use it either — match the renderer.
    glm::vec3 position, scale, skew;
    glm::quat rotation;
    glm::vec4 perspective;
    glm::decompose(d.local_transform, scale, rotation, position, skew,
                   perspective);

    const glm::quat applied_rot = glm::quat(d.applied_rotation);
    const glm::quat final_rot = glm::normalize(rotation * applied_rot);
    const glm::vec3 final_scale = scale * d.applied_scale;

    const glm::mat4 T = glm::translate(glm::mat4(1.0f), position);
    const glm::mat4 R = glm::mat4_cast(final_rot);
    const glm::mat4 S = glm::scale(glm::mat4(1.0f), final_scale);
    const glm::mat4 local_world = T * R * S;

    // Apply whole-model scale
    const glm::mat4 M_inst =
        glm::scale(glm::mat4(1.0f), result.model.model_scale);
    const glm::mat4 final_transform = M_inst * local_world;

    const InstanceGeometry* inst_geom = nullptr;
    for (const auto& ig : result.instance_geometry) {
      if (ig.descriptor_id == d.descriptor_id) {
        inst_geom = &ig;
        break;
      }
    }

    if (inst_geom) {
      for (const auto& mesh : inst_geom->mesh_data) {
        AABB mesh_box = ComputeMeshAABB(mesh, final_transform);
        MergeAABB(part_box, mesh_box);
      }
    } else {
      for (int mesh_idx : d.mesh_indices) {
        if (mesh_idx < 0 ||
            mesh_idx >= static_cast<int>(graph.mesh_data.size()))
          continue;
        AABB mesh_box =
            ComputeMeshAABB(graph.mesh_data[mesh_idx], final_transform);
        MergeAABB(part_box, mesh_box);
      }
    }

    auto parsed = ParseDescriptorId(d.descriptor_id);
    MergeAABB(out.per_part_bounds[parsed.part_id], part_box);
    MergeAABB(out.per_group_bounds[d.group_id], part_box);
    MergeAABB(out.model_bounds, part_box);
  }
}

//
// Populate raw selection/parameter data on the result.
//
static void RecordRawData(const InstanceModel& result,
                          const ParameterSampler& sampler,
                          ProcModelSample& out) {
  // Collapse locator-expanded descriptors back to their authored part.
  // Generator produces one ResolvedDescriptor per resolved locator, so a
  // single authored part can appear multiple times in resolved.descriptors
  // (once per "_at_<locator>" suffix). We merge them into one PartSample
  // and accumulate the locator list.
  std::unordered_map<std::string, ProcModelSample::PartSample> by_part;
  std::unordered_set<std::string> unique_parts;

  for (const auto& d : result.descriptors) {
    auto parsed = ParseDescriptorId(d.descriptor_id);
    unique_parts.insert(parsed.part_id);

    auto it = by_part.find(parsed.part_id);
    if (it == by_part.end()) {
      ProcModelSample::PartSample entry;
      entry.descriptor_id = parsed.part_id;
      entry.group_id = d.group_id;
      entry.applied_rotation = d.applied_rotation;
      entry.applied_scale = d.applied_scale;

      if (!parsed.locator.empty())
        entry.locators.push_back(parsed.locator);

      auto sit = sampler.ByDescriptor().find(d.descriptor_id);
      if (sit != sampler.ByDescriptor().end())
        entry.parameter_samples = sit->second;

      by_part.emplace(parsed.part_id, std::move(entry));
    } else {
      // Accumulate additional locators for the same authored part
      if (!parsed.locator.empty())
        it->second.locators.push_back(parsed.locator);

      auto sit = sampler.ByDescriptor().find(d.descriptor_id);
      if (sit != sampler.ByDescriptor().end()) {
        for (const auto& p : sit->second)
          it->second.parameter_samples.push_back(p);
      }
    }
  }

  out.part_samples.reserve(by_part.size());
  for (auto& [_, entry] : by_part) {
    out.part_samples.push_back(std::move(entry));
  }

  out.selected_part_ids.assign(unique_parts.begin(), unique_parts.end());
  std::sort(out.selected_part_ids.begin(), out.selected_part_ids.end());
}

//
// Public entry point
//
ProcModelSample ProcModelValidator::Validate(const InstanceData& data,
                                             const ModelGraph& graph,
                                             const ModelDescriptor& descriptor,
                                             int attempt_index,
                                             ParameterSampler& sampler) {

  ProcModelSample out;
  out.model_id = data.model.model_id;
  out.seed = data.model.seed;
  out.attempt_index = attempt_index;
  out.timestamp_ms = NowMs();

  RecordRawData(data.model, sampler, out);
  out.model_samples = sampler.Instance();

  CheckConstraints(data.model, descriptor, out);
  CheckLocators(data.model, graph, out);
  CheckGroupActivation(data.model, descriptor, out);
  CheckForwardAxisConsistency(graph, descriptor, out);
  ComputeBounds(data, graph, out);

  // Overall pass = no Error-severity diagnostics
  out.passed = std::none_of(out.diagnostics.begin(), out.diagnostics.end(),
                            [](const Diagnostic& d) {
                              return d.severity == Diagnostic::Severity::Error;
                            });

  // Sort active_group_ids for deterministic output
  std::sort(out.active_group_ids.begin(), out.active_group_ids.end());
  out.active_group_ids.erase(
      std::unique(out.active_group_ids.begin(), out.active_group_ids.end()),
      out.active_group_ids.end());

  return out;
}

}  // namespace ProcModel