#include "model_descriptor_parser.h"

#include <glm/glm.hpp>
#include "glm/ext/vector_float3.hpp"

#include "engine/core/logger.h"
#include "engine/pcg/procmodel/descriptor/model_descriptor.h"

namespace ProcModel {

bool ModelDescriptorParser::ParseSelectionGroup(const nlohmann::json& j,
                                                SelectionGroup& out) {
  if (!j.contains("group_id") || !j.contains("parts"))
    return false;

  out.group_id = j["group_id"].get<std::string>();
  out.required = j.value("required", true);
  out.unique_per_locator = j.value("unique_per_locator", false);
  out.list = j.value("list", 0);

  // parent: accept array or omitted
  if (j.contains("parent")) {
    const auto& p = j["parent"];
    if (p.is_array()) {
      for (const auto& id : p) {
        out.parent.push_back(id.get<std::string>());
      }
    } else if (p.is_string()) {
      // backwards compat: single string -> wrap as one-element vector
      out.parent.push_back(p.get<std::string>());
    }
  }

  out.scale_factor = j.value("scale_factor", 1.0f);
  if (j.contains("keep")) {
    const auto& k = j["keep"];
    out.keep = glm::ivec2(k[0].get<int>(), k[1].get<int>());
  }
  if (j.contains("scale_jitter")) {
    out.scale_jitter = j["scale_jitter"].get<float>();
  }

  if (j.contains("rotation_jitter")) {
    const auto& r = j["rotation_jitter"];
    out.rotation_jitter = glm::radians(glm::vec3(r[0], r[1], r[2]));
  }

  if (j.contains("locators")) {
    std::vector<std::string> locators;
    for (const auto& s : j["locators"]) {
      locators.push_back(s.get<std::string>());
    }
    out.locators = std::move(locators);
  }

  for (const auto& part_json : j["parts"]) {
    PartDescriptor part;
    part.id = part_json["id"].get<std::string>();
    part.name = part_json.value("name", part.id);
    part.weight = part_json.value("weight", 1.0f);
    part.key = part_json.value("key", "");
    out.parts.push_back(std::move(part));
  }
  return !out.parts.empty();
}

bool ModelDescriptorParser::ParseTransformRange(const nlohmann::json& j,
                                                TransformRange& out) {
  if (!j.contains("part_id"))
    return false;

  out.part_id = j["part_id"].get<std::string>();

  if (j.contains("rotation_min")) {
    auto& r = j["rotation_min"];
    out.rotation_min = glm::radians(glm::vec3(r[0], r[1], r[2]));
  }
  if (j.contains("rotation_max")) {
    auto& r = j["rotation_max"];
    out.rotation_max = glm::radians(glm::vec3(r[0], r[1], r[2]));
  }

  return true;
}

bool ModelDescriptorParser::ParseConstraintRule(const nlohmann::json& j,
                                                ConstraintRule& out) {
  if (!j.contains("id") || !j.contains("type") || !j.contains("part_a") ||
      !j.contains("part_b"))
    return false;

  out.id = j["id"].get<std::string>();
  out.part_a = j["part_a"].get<std::string>();
  out.part_b = j["part_b"].get<std::string>();

  std::string type_str = j["type"].get<std::string>();
  if (type_str == "excludes") {
    out.type = ConstraintRule::Type::EXCLUDES;
  } else if (type_str == "requires") {
    out.type = ConstraintRule::Type::REQUIRES;
  } else {
    return false;
  }

  return true;
}

bool ModelDescriptorParser::ParseParameterBinding(const nlohmann::json& j,
                                                  ParameterBinding& out) {
  if (!j.contains("source_part") || !j.contains("target_part"))
    return false;

  out.source_part = j["source_part"].get<std::string>();
  out.source_param = j.value("source_param", std::string(""));
  out.target_part = j["target_part"].get<std::string>();
  out.target_param = j.value("target_param", std::string(""));
  out.ratio = j.value("ratio", 1.0f);

  return true;
}

bool ModelDescriptorParser::ParseDeformationRange(const nlohmann::json& j,
                                                  DeformationRange& out) {
  // group_id is optional here: empty for the default, populated for overrides.
  out.group_id = j.value("group_id", std::string(""));

  auto parse_float_range = [&](const std::string& kmin, const std::string& kmax,
                               std::optional<float>& dst_min,
                               std::optional<float>& dst_max) {
    if (j.contains(kmin))
      dst_min = j[kmin].get<float>();
    if (j.contains(kmax))
      dst_max = j[kmax].get<float>();
  };

  parse_float_range("taper_factor_min", "taper_factor_max",
                    out.taper_factor_min, out.taper_factor_max);
  parse_float_range("twist_angle_min", "twist_angle_max", out.twist_angle_min,
                    out.twist_angle_max);
  parse_float_range("bend_angle_min", "bend_angle_max", out.bend_angle_min,
                    out.bend_angle_max);
  parse_float_range("noise_amplitude_min", "noise_amplitude_max",
                    out.noise_amplitude_min, out.noise_amplitude_max);

  // Convert angle ranges from degrees to radians
  if (out.twist_angle_min)
    out.twist_angle_min = glm::radians(*out.twist_angle_min);
  if (out.twist_angle_max)
    out.twist_angle_max = glm::radians(*out.twist_angle_max);
  if (out.bend_angle_min)
    out.bend_angle_min = glm::radians(*out.bend_angle_min);
  if (out.bend_angle_max)
    out.bend_angle_max = glm::radians(*out.bend_angle_max);

  return true;
}

bool ModelDescriptorParser::FromJson(const nlohmann::json& j,
                                     ModelDescriptor& out) {
  if (!j.contains("model_id")) {
    Logger::getInstance().Log(LogLevel::Error,
                              "[ModelDescriptorParser] Missing model_id");
    return false;
  }

  out.model_id = j["model_id"].get<std::string>();
  out.model_name = j.value("model_name", out.model_id);
  out.domain = j.value("domain", std::string(""));
  out.path = j.value("path", std::string(""));
  out.max_depth = j.value("max_depth", out.max_depth);
  if (j.contains("distortion"))
    out.distortion = j["distortion"].get<float>();
  if (j.contains("textures"))
    for (const auto& [material, path] : j["textures"].items())
      out.textures[material] = path.get<std::string>();
  if (j.contains("poses")) {
    const auto& p = j["poses"];
    PoseSet& poses = out.poses.emplace();
    poses.clip = p.value("clip", std::string());
    for (const auto& a : p.value("anchors", nlohmann::json::array())) {
      PoseSet::Anchor& anchor = poses.anchors.emplace_back();
      anchor.name = a.value("name", std::string());
      if (a.contains("value") && a["value"].is_string())
        anchor.source = a["value"].get<std::string>();
      else
        anchor.value = a.value("value", anchor.value);
      anchor.spread = a.value("spread", anchor.spread);
    }
    for (const auto& i : p.value("items", nlohmann::json::array()))
      poses.items.push_back({i.value("name", std::string()),
                             i.value("start", -1), i.value("end", -1),
                             i.value("spread", 0.325f)});
    for (const auto& l : p.value("links", nlohmann::json::array()))
      poses.links.push_back({l.value("a", std::string()),
                             l.value("b", std::string()),
                             l.value("correlation", 0.0f)});
  }
  if (j.contains("movement")) {
    const auto& m = j["movement"];
    GaitSet& gaits = out.gaits.emplace();
    gaits.speed_scale = m.value("speed_scale", gaits.speed_scale);
    for (const auto& g : m.value("gaits", nlohmann::json::array())) {
      GaitSet::Gait& gait = gaits.gaits.emplace_back();
      gait.name = g.value("name", std::string());
      gait.clip = g.value("clip", std::string());
      gait.pace = g.value("pace", gait.pace);
      gait.min_rate = g.value("min_rate", gait.min_rate);
      gait.max_rate = g.value("max_rate", gait.max_rate);
    }
  }

  // Whole-model scale range
  if (j.contains("scale_min")) {
    auto& s = j["scale_min"];
    out.scale_min = glm::vec3(s[0], s[1], s[2]);
  }
  if (j.contains("scale_max")) {
    auto& s = j["scale_max"];
    out.scale_max = glm::vec3(s[0], s[1], s[2]);
  }

  // Selection groups
  if (j.contains("selection_groups")) {
    for (const auto& group_json : j["selection_groups"]) {
      SelectionGroup group;
      if (ParseSelectionGroup(group_json, group)) {
        out.selection_groups.push_back(std::move(group));
      } else {
        Logger::getInstance().Log(
            LogLevel::Warning,
            "[ModelDescriptorParser] Failed to parse selection group");
      }
    }
  }

  if (j.contains("locator_operations")) {
    for (const auto& mod_json : j["locator_operations"]) {
      if (!mod_json.contains("kind")) {
        Logger::getInstance().Log(LogLevel::Warning,
                                  "[ModelDescriptorParser] locator_modifier "
                                  "missing 'kind', skipping");
        continue;
      }
      LocatorOperation sm;
      sm.target_group_id = mod_json.value("target_group_id", std::string(""));
      sm.entry.kind = mod_json["kind"].get<std::string>();
      sm.entry.params = mod_json.value("params", nlohmann::json::object());
      out.locator_operations.push_back(std::move(sm));
    }
  }

  if (j.contains("transform_ranges")) {
    for (const auto& range_json : j["transform_ranges"]) {
      TransformRange range;
      if (ParseTransformRange(range_json, range)) {
        out.transform_ranges.push_back(std::move(range));
      } else {
        Logger::getInstance().Log(
            LogLevel::Warning,
            "[ModelDescriptorParser] Failed to parse parameter range");
      }
    }
  }

  if (j.contains("constraints")) {
    for (const auto& rule_json : j["constraints"]) {
      ConstraintRule rule;
      if (ParseConstraintRule(rule_json, rule)) {
        out.constraints.push_back(std::move(rule));
      } else {
        Logger::getInstance().Log(
            LogLevel::Warning,
            "[ModelDescriptorParser] Failed to parse constraint rule");
      }
    }
  }

  if (j.contains("parameter_bindings")) {
    for (const auto& binding_json : j["parameter_bindings"]) {
      ParameterBinding binding;
      if (ParseParameterBinding(binding_json, binding)) {
        out.parameter_bindings.push_back(std::move(binding));
      } else {
        Logger::getInstance().Log(
            LogLevel::Warning,
            "[ModelDescriptorParser] Failed to parse parameter binding");
      }
    }
  }

  if (j.contains("model_deformation_range")) {
    DeformationRange dr;
    if (ParseDeformationRange(j["model_deformation_range"], dr)) {
      out.model_deformation_range = std::move(dr);
    }
  }

  if (j.contains("part_deformation_ranges")) {
    for (const auto& range_json : j["part_deformation_ranges"]) {
      DeformationRange range;
      if (ParseDeformationRange(range_json, range)) {
        out.part_deformation_ranges.push_back(std::move(range));
      } else {
        Logger::getInstance().Log(
            LogLevel::Warning,
            "[ModelDescriptorParser] Failed to parse part deformation range");
      }
    }
  }

  return true;
}

}  // namespace ProcModel
