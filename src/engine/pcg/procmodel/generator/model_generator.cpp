#include "model_generator.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <random>
#include <set>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "content/creature_pose.h"
#include "content/model_parts.h"

#include "engine/pcg/pipeline/linear_pipeline.h"
#include "engine/pcg/procmodel/generator/model_context.h"
#include "engine/pcg/procmodel/generator/model_generator.h"

#include "engine/core/logger.h"

namespace ProcModel {
namespace {

struct PendingActivation {
  const SelectionGroup* group;
  std::string activator_instance_id;
  std::string activator_part_id;
  glm::mat4 activator_world_transform;
  std::string activator_key;  // the activator's place in the pick tree
  int depth;
};

// Parts in the pick tree are keyed by where they sit: the activating
// instance's key, the group, the locator when each locator picks its own
// part, then the part id
std::string SlotKey(const std::string& activator_key,
                    const SelectionGroup& group, const std::string& locator) {
  std::string key = activator_key + "/" + group.group_id;
  if (!locator.empty())
    key += "@" + locator;
  return key;
}

std::string InstanceKey(const std::string& part_key,
                        const std::string& locator) {
  return locator.empty() ? part_key : part_key + "@" + locator;
}

bool Activates(const SelectionGroup& group, const std::string& part_id) {
  if (part_id.empty())
    return group.parent.empty();
  return std::find(group.parent.begin(), group.parent.end(), part_id) !=
         group.parent.end();
}

const std::vector<std::string>& Locators(const SelectionGroup& group) {
  static const std::vector<std::string> none{""};
  return group.locators && !group.locators->empty() ? *group.locators : none;
}

// The groups of one list a part instance activates (the root: all root
// groups), as a worldgen part list: one type per pick (per locator when the
// group picks per locator), and under each part the lists its own instances
// activate, in list order, down to max_depth
struct PickTree {
  const ModelDescriptor& descriptor;
  size_t parts = 0;
  static constexpr size_t part_limit = 200000;

  Worldgen::PartList Build(const std::string& activator_key,
                           const std::string& activator_part_id, int depth,
                           int list_index = 0) {
    Worldgen::PartList list;
    for (const auto& group : descriptor.selection_groups) {
      if (!Activates(group, activator_part_id) ||
          (!activator_part_id.empty() && group.list != list_index))
        continue;
      const auto& locators = Locators(group);
      if (group.unique_per_locator) {
        for (const auto& locator : locators)
          list.types.push_back(Type(
              group, SlotKey(activator_key, group, locator), {""}, depth));
      } else {
        list.types.push_back(
            Type(group, SlotKey(activator_key, group, ""), locators, depth));
      }
    }
    return list;
  }

  Worldgen::PartType Type(const SelectionGroup& group, const std::string& slot,
                          const std::vector<std::string>& instances,
                          int depth) {
    Worldgen::PartType type;
    for (const auto& part : group.parts) {
      Worldgen::Part p;
      p.id = slot + ":" + part.id;
      p.key = part.key;
      // worldgen weighs in integers
      p.weight = part.weight > 0.0f
                     ? std::max(1, static_cast<int>(
                                       std::lround(part.weight * 1000.0f)))
                     : 0;
      if (depth + 1 < descriptor.max_depth && ++parts < part_limit) {
        std::set<int> lists;
        for (const auto& g : descriptor.selection_groups)
          if (Activates(g, part.id) && !g.parent.empty())
            lists.insert(g.list);
        for (const auto& locator : instances)
          for (const int index : lists) {
            Worldgen::PartList child =
                Build(InstanceKey(p.id, locator), part.id, depth + 1, index);
            if (!child.types.empty())
              p.children.push_back(std::move(child));
          }
      }
      type.parts.push_back(std::move(p));
    }
    return type;
  }
};

std::unordered_set<std::string> PickKeys(const ModelDescriptor& descriptor,
                                         uint64_t seed) {
  PickTree tree{descriptor};
  const Worldgen::PartList root = tree.Build("", "", 0);
  if (tree.parts >= PickTree::part_limit)
    Logger::getInstance().Log(
        LogLevel::Warning,
        "[Generator] " + descriptor.model_id +
            ": over 200000 parts to pick from, deeper levels are cut; "
            "lower max_depth");
  const Worldgen::PartLoader load = [&root](const char*) { return &root; };
  const std::vector<std::string> picks =
      descriptor.distortion
          ? Worldgen::PickParts(load, "", seed, true, *descriptor.distortion)
          : Worldgen::PickPartsSingle(load, "", seed, true);
  return {picks.begin(), picks.end()};
}

// The part's transform in model space, with its sampled rotation and scale,
// composed as ModelInstantiator composes it
glm::mat4 PartWorld(const ResolvedDescriptor& d) {
  glm::vec3 scale, position, skew;
  glm::quat rotation;
  glm::vec4 perspective;
  glm::decompose(d.local_transform, scale, rotation, position, skew,
                 perspective);
  return glm::translate(glm::mat4(1.0f), position) *
         glm::mat4_cast(rotation * glm::quat(d.applied_rotation)) *
         glm::scale(glm::mat4(1.0f), scale * d.applied_scale);
}

}  // namespace

// Final matrix is composed in ModelInstantiator
static void ApplyParameterRanges(ResolvedDescriptor& resolved,
                                 const ModelGraphNode& node, pcg32& rng,
                                 ParameterSampler& sampler) {
  for (const auto* range : node.transform_ranges) {
    if (range->rotation_min && range->rotation_max) {
      float rx =
          sampler.Uniform("transform_rotation", "x", range->rotation_min->x,
                          range->rotation_max->x, rng);
      float ry =
          sampler.Uniform("transform_rotation", "y", range->rotation_min->y,
                          range->rotation_max->y, rng);
      float rz =
          sampler.Uniform("transform_rotation", "z", range->rotation_min->z,
                          range->rotation_max->z, rng);
      resolved.applied_rotation += glm::vec3(rx, ry, rz);
    }
    if (range->scale_min && range->scale_max) {
      float sx = sampler.Uniform("transform_scale", "x", range->scale_min->x,
                                 range->scale_max->x, rng);
      float sy = sampler.Uniform("transform_scale", "y", range->scale_min->y,
                                 range->scale_max->y, rng);
      float sz = sampler.Uniform("transform_scale", "z", range->scale_min->z,
                                 range->scale_max->z, rng);
      resolved.applied_scale *= glm::vec3(sx, sy, sz);
    }
  }
}

// The group's part picked at this slot, or nullptr
static const PartDescriptor* Picked(
    const SelectionGroup& group, const std::string& slot,
    const std::unordered_set<std::string>& picked) {
  for (const auto& part : group.parts) {
    if (picked.count(slot + ":" + part.id))
      return &part;
  }
  return nullptr;
}

std::optional<InstanceData> ModelGenerator::Generate(
    const ModelGraph& graph, const ModelDescriptor& descriptor,
    const PCG::LinearPipeline& pipeline,
    const PCG::OperationRegistry& operation_registry, uint64_t seed,
    int max_retries, std::vector<ProcModelSample>* out_samples) {

  for (int attempt = 0; attempt < max_retries; ++attempt) {
    pcg32 rng(seed + attempt);
    ParameterSampler sampler;

    // Which part each slot takes, by worldgen's walk over the whole tree;
    // rng samples the transforms and operations
    const std::unordered_set<std::string> picked =
        PickKeys(descriptor, seed + attempt);

    std::unordered_set<std::string> selected_ids;
    std::vector<ResolvedDescriptor> resolved_descriptors;
    bool failed = false;

    // Collect root groups
    std::queue<PendingActivation> pending;
    for (const auto& group : descriptor.selection_groups) {
      if (group.parent.empty()) {
        pending.push({&group, "", "", glm::mat4(1.0f), "", 0});
      }
    }

    while (!pending.empty() && !failed) {
      const PendingActivation pa = pending.front();
      pending.pop();

      const SelectionGroup* group = pa.group;

      // Authored activator world. We use this to extract each placement's
      // *local* offset from the activator as it sits in the GLTF
      // (authored_inverse * node world), then re-apply that offset from the
      // placed activator's world transform.
      glm::mat4 authored_activator_world(1.0f);
      if (!pa.activator_part_id.empty()) {
        auto act_it = graph.node_lookup.find(pa.activator_part_id);
        if (act_it != graph.node_lookup.end()) {
          authored_activator_world = act_it->second->world_transform;
        }
      }
      glm::mat4 authored_inverse = glm::inverse(authored_activator_world);

      // Compile locator modifiers for this group from descriptor-level
      // entries. Entries with empty target_group_id apply to all groups.
      std::vector<std::pair<PCG::OperationRegistry::Handler,
                            std::shared_ptr<PCG::OperationData>>>
          compiled_modifiers;
      for (const auto& mod : descriptor.locator_operations) {
        if (!mod.target_group_id.empty() &&
            mod.target_group_id != group->group_id)
          continue;

        auto data = operation_registry.Parse(mod.entry.kind, mod.entry.params);
        auto handler = operation_registry.GetHandler(mod.entry.kind);
        if (!data || !handler) {
          Logger::getInstance().Log(
              LogLevel::Warning,
              "[Generator] Unknown or unparseable locator modifier '" +
                  mod.entry.kind + "' in group '" + group->group_id +
                  "'; skipping");
          continue;
        }
        compiled_modifiers.emplace_back(std::move(handler), std::move(data));
      }

      struct Placement {
        ResolvedDescriptor resolved;
        std::string part_id;
        std::string key;  // the instance's key in the pick tree
      };
      std::vector<Placement> newly_added;

      // Variant-only groups (no locators) place the part where it is
      // authored, once per activating instance; locator groups place one
      // instance per locator
      for (const auto& locator_id : Locators(*group)) {
        const std::string slot =
            SlotKey(pa.activator_key, *group,
                    group->unique_per_locator ? locator_id : std::string());
        const PartDescriptor* chosen = Picked(*group, slot, picked);
        if (!chosen) {
          failed = group->required;
          if (failed || !group->unique_per_locator)
            break;
          continue;
        }
        auto it = graph.node_lookup.find(chosen->id);
        if (it == graph.node_lookup.end()) {
          failed = true;
          break;
        }
        const ModelGraphNode* node = it->second;
        selected_ids.insert(chosen->id);

        ResolvedDescriptor resolved;
        resolved.group_id = group->group_id;
        resolved.mesh_indices = node->mesh_indices;
        resolved.activator_id = pa.activator_instance_id;

        if (locator_id.empty()) {
          resolved.descriptor_id = chosen->id;
          resolved.local_transform = pa.activator_world_transform *
                                     authored_inverse * node->world_transform;
        } else {
          auto sock_it = graph.node_lookup.find(locator_id);
          if (sock_it == graph.node_lookup.end())
            continue;

          // The locator's offset from its authored activator, re-applied
          // from the placed activator
          glm::mat4 locator_world = pa.activator_world_transform *
                                    authored_inverse *
                                    sock_it->second->world_transform;
          resolved.descriptor_id = chosen->id + "_at_" + locator_id;

          // Run authored locator modifiers (radial_align, jitter, etc)
          sampler.Set(resolved.descriptor_id);
          if (!compiled_modifiers.empty()) {
            LocatorContext locator_ctx(locator_world,
                                       pa.activator_world_transform,
                                       locator_id, rng, sampler);
            for (const auto& [handler, data] : compiled_modifiers) {
              handler(*data, locator_ctx);
            }
          }
          resolved.local_transform = locator_world;

          // Sample scale factor with optional jitter
          float factor = group->scale_factor;
          if (group->scale_jitter) {
            factor += sampler.Uniform("scale_jitter", "", -*group->scale_jitter,
                                      *group->scale_jitter, rng);
          }
          resolved.applied_scale = glm::vec3(factor);
        }
        if (!pa.activator_instance_id.empty())
          resolved.descriptor_id += "_under_" + pa.activator_instance_id;

        // Sample parameter ranges, so each placed copy gets independent
        // rotation/scale jitter
        sampler.Set(resolved.descriptor_id);
        ApplyParameterRanges(resolved, *node, rng, sampler);

        // Per-attachment rotation jitter
        const glm::vec3& j = group->rotation_jitter;
        if (!locator_id.empty() && (j.x > 0.0f || j.y > 0.0f || j.z > 0.0f)) {
          float rx =
              sampler.Uniform("rotation_jitter_legacy", "x", -j.x, j.x, rng);
          float ry =
              sampler.Uniform("rotation_jitter_legacy", "y", -j.y, j.y, rng);
          float rz =
              sampler.Uniform("rotation_jitter_legacy", "z", -j.z, j.z, rng);
          resolved.applied_rotation += glm::vec3(rx, ry, rz);
        }

        newly_added.push_back(
            {std::move(resolved), chosen->id,
             InstanceKey(slot + ":" + chosen->id,
                         group->unique_per_locator ? "" : locator_id)});
      }
      if (failed)
        break;

      // Keep a drawn number of the placements, in a drawn order
      if (group->keep && !newly_added.empty()) {
        Worldgen::NodeGroup kept{true, group->keep->x, group->keep->y,
                                 static_cast<int>(newly_added.size())};
        const uint64_t keep_seed =
            static_cast<uint64_t>(rng()) << 32 | static_cast<uint64_t>(rng());
        Worldgen::NodeDistribution(keep_seed, true, &kept, 1);
        std::vector<Placement> chosen;
        for (int k = 0; k < std::min(kept.kept, kept.nodes); ++k)
          chosen.push_back(std::move(newly_added[kept.order[k]]));
        newly_added = std::move(chosen);
      }

      // Activate child groups per placed instance, from its full transform
      if (pa.depth + 1 < descriptor.max_depth) {
        for (const auto& placement : newly_added) {
          for (const auto& g : descriptor.selection_groups) {
            if (Activates(g, placement.part_id))
              pending.push({&g, placement.resolved.descriptor_id,
                            placement.part_id, PartWorld(placement.resolved),
                            placement.key, pa.depth + 1});
          }
        }
      }
      for (auto& placement : newly_added) {
        resolved_descriptors.push_back(std::move(placement.resolved));
      }
    }

    Logger::getInstance().Log(
        LogLevel::Debug,
        "[Generator][Diag] attempt=" + std::to_string(attempt) +
            " seed=" + std::to_string(seed + attempt) +
            " failed=" + std::to_string(failed ? 1 : 0) +
            " selected=" + std::to_string(selected_ids.size()) +
            " resolved=" + std::to_string(resolved_descriptors.size()));

    if (failed)
      continue;

    // Apply parameter bindings across resolved descriptors
    ApplyParameterBindings(resolved_descriptors, descriptor.parameter_bindings);

    InstanceModel result;
    result.model_id = descriptor.model_id;
    result.seed = seed + attempt;
    result.descriptors = std::move(resolved_descriptors);

    // TODO: revise the if condition here
    if (descriptor.scale_min && descriptor.scale_max) {
      std::uniform_real_distribution<float> dx(descriptor.scale_min->x,
                                               descriptor.scale_max->x);
      std::uniform_real_distribution<float> dy(descriptor.scale_min->y,
                                               descriptor.scale_max->y);
      std::uniform_real_distribution<float> dz(descriptor.scale_min->z,
                                               descriptor.scale_max->z);
      result.model_scale = glm::vec3(dx(rng), dy(rng), dz(rng));
    } else {
      result.model_scale = glm::vec3(1.0f);
    }

    // Run pipeline operations on resolved model
    {
      ModelContext ctx(descriptor, graph, result, rng, sampler);
      pipeline.Run(ctx);

      // Final constraint check
      if (ValidateConstraints(selected_ids, descriptor.constraints)) {

        InstanceData out;
        out.model = std::move(result);
        out.instance_geometry = std::move(ctx.instance_geometry);

        // Post-generation validation
        if (out_samples) {
          ProcModelSample vr = ProcModelValidator::Validate(
              out, graph, descriptor, attempt, sampler);

          out_samples->push_back(vr);
          if (!vr.passed) {
            std::string reason = "unknown";
            for (const auto& d : vr.diagnostics) {
              if (d.severity == Diagnostic::Severity::Error) {
                reason = d.code;
                break;
              }
            }
            Logger::getInstance().Log(
                LogLevel::Warning,
                "[Generator] Attempt " + std::to_string(attempt) +
                    " rejected: " + reason +
                    " (seed=" + std::to_string(seed + attempt) + ")");
            continue;  // Retry with next seed
          }

          Logger::getInstance().Log(
              LogLevel::Debug,
              "[Generator] Attempt " + std::to_string(attempt) +
                  " accepted (seed=" + std::to_string(seed + attempt) + ")");
        }

        Logger::getInstance().Log(
            LogLevel::Debug,
            "[Generator] Returning InstanceData: descriptors=" +
                std::to_string(out.model.descriptors.size()) +
                " geometry_count=" +
                std::to_string(out.instance_geometry.size()));

        return out;
      }
    }
  }

  return std::nullopt;
}

void ModelGenerator::ApplyParameterBindings(
    std::vector<ResolvedDescriptor>& descriptors,
    const std::vector<ParameterBinding>& bindings) {
  std::unordered_map<std::string, ResolvedDescriptor*> lookup;
  for (auto& desc : descriptors) {
    lookup[desc.descriptor_id] = &desc;
  }

  for (const auto& binding : bindings) {
    auto src_it = lookup.find(binding.source_part);
    auto tgt_it = lookup.find(binding.target_part);

    if (src_it == lookup.end() || tgt_it == lookup.end())
      continue;

    if (binding.source_param == "rotation" &&
        binding.target_param == "rotation") {
      tgt_it->second->applied_rotation =
          src_it->second->applied_rotation * binding.ratio;
    }
  }
}

bool ModelGenerator::ValidateConstraints(
    const std::unordered_set<std::string>& selected_ids,
    const std::vector<ConstraintRule>& constraints) {
  for (const auto& rule : constraints) {
    bool a_selected = selected_ids.count(rule.part_a) > 0;
    bool b_selected = selected_ids.count(rule.part_b) > 0;

    switch (rule.type) {
      case ConstraintRule::Type::EXCLUDES:
        if (a_selected && b_selected)
          return false;
        break;
      case ConstraintRule::Type::REQUIRES:
        if (a_selected && !b_selected)
          return false;
        if (b_selected && !a_selected)
          return false;
        break;
    }
  }
  return true;
}

}  // namespace ProcModel
