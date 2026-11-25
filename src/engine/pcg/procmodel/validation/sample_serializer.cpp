#include "sample_serializer.h"

namespace ProcModel {

static glm::vec3 SumByOpKind(const std::vector<ParameterSample>& samples,
                             const std::string& op_kind) {
  glm::vec3 out(0.0f);
  for (const auto& p : samples) {
    if (p.op_kind != op_kind)
      continue;
    if (p.axis == "x")
      out.x += p.sampled;
    else if (p.axis == "y")
      out.y += p.sampled;
    else if (p.axis == "z")
      out.z += p.sampled;
  }
  return out;
}

static glm::vec3 ProductByOpKind(const std::vector<ParameterSample>& samples,
                                 const std::string& op_kind) {
  glm::vec3 out(1.0f);
  for (const auto& p : samples) {
    if (p.op_kind != op_kind)
      continue;
    if (p.axis == "x")
      out.x *= p.sampled;
    else if (p.axis == "y")
      out.y *= p.sampled;
    else if (p.axis == "z")
      out.z *= p.sampled;
  }
  return out;
}

//
// JSON serialization
//
static nlohmann::json SerializeAABB(const AABB& box) {
  nlohmann::json j;
  j["valid"] = box.valid;
  if (box.valid) {
    j["min"] = {box.min.x, box.min.y, box.min.z};
    j["max"] = {box.max.x, box.max.y, box.max.z};
  }
  return j;
}

static nlohmann::json SerializeDiagnostic(const Diagnostic& d) {
  nlohmann::json j;
  j["code"] = d.code;
  j["severity"] =
      d.severity == Diagnostic::Severity::Error ? "error" : "warning";
  j["part_ids"] = d.part_ids;
  if (!d.group_id.empty())
    j["group_id"] = d.group_id;
  j["message"] = d.message;
  return j;
}

static nlohmann::json SerializePartSample(
    const ProcModelSample::PartSample& e) {
  nlohmann::json j;
  j["descriptor_id"] = e.descriptor_id;
  j["group_id"] = e.group_id;

  const glm::vec3 applied_rotation =
      SumByOpKind(e.parameter_samples, "transform_rotation");
  const glm::vec3 applied_scale =
      ProductByOpKind(e.parameter_samples, "transform_scale");

  j["applied_rotation"] = {applied_rotation.x, applied_rotation.y,
                           applied_rotation.z};
  j["applied_scale"] = {applied_scale.x, applied_scale.y, applied_scale.z};
  j["locators"] = e.locators;

  nlohmann::json params = nlohmann::json::array();
  for (const auto& p : e.parameter_samples) {
    params.push_back({
        {"op_kind", p.op_kind},
        {"axis", p.axis},
        {"sampled", p.sampled},
        {"range_min", p.range_min},
        {"range_max", p.range_max},
    });
  }
  j["parameter_samples"] = std::move(params);

  return j;
}

nlohmann::json SerializeSample(const ProcModelSample& r) {
  nlohmann::json j;

  // Identity
  j["model_id"] = r.model_id;
  j["seed"] = r.seed;
  j["attempt_index"] = r.attempt_index;
  j["timestamp_ms"] = r.timestamp_ms;

  // Outcome
  j["passed"] = r.passed;
  j["outcome"] = r.passed ? "accepted" : "rejected";

  if (!r.passed) {
    nlohmann::json reasons = nlohmann::json::array();
    for (const auto& d : r.diagnostics) {
      if (d.severity == Diagnostic::Severity::Error) {
        reasons.push_back(d.code);
      }
    }
    if (!reasons.empty()) {
      j["rejection_reasons"] = std::move(reasons);
    }
  }

  nlohmann::json diags = nlohmann::json::array();
  for (const auto& d : r.diagnostics) {
    diags.push_back(SerializeDiagnostic(d));
  }
  j["diagnostics"] = std::move(diags);

  // Raw selection data
  j["selected_part_ids"] = r.selected_part_ids;
  j["active_group_ids"] = r.active_group_ids;

  // Raw parameter data
  nlohmann::json entries = nlohmann::json::array();
  for (const auto& e : r.part_samples) {
    entries.push_back(SerializePartSample(e));
  }
  j["part_samples"] = std::move(entries);

  // Geometry
  j["model_bounds"] = SerializeAABB(r.model_bounds);

  nlohmann::json per_part = nlohmann::json::object();
  for (const auto& [id, box] : r.per_part_bounds) {
    per_part[id] = SerializeAABB(box);
  }
  j["per_part_bounds"] = std::move(per_part);

  nlohmann::json per_group = nlohmann::json::object();
  for (const auto& [id, box] : r.per_group_bounds) {
    per_group[id] = SerializeAABB(box);
  }
  j["per_group_bounds"] = std::move(per_group);

  nlohmann::json instance_params = nlohmann::json::array();
  for (const auto& p : r.model_samples) {
    instance_params.push_back({
        {"op_kind", p.op_kind},
        {"axis", p.axis},
        {"sampled", p.sampled},
        {"range_min", p.range_min},
        {"range_max", p.range_max},
    });
  }
  j["model_samples"] = std::move(instance_params);

  return j;
}

}  // namespace ProcModel
