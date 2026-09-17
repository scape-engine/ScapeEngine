#include "vertex_deform.h"

#include <glm/gtc/constants.hpp>
#include <nlohmann/json.hpp>
#include <random>
#include <string>

#include "engine/core/logger.h"
#include "engine/pcg/pipeline/operation_context.h"
#include "engine/pcg/pipeline/operation_registry.h"
#include "engine/pcg/procmodel/descriptor/model_descriptor.h"
#include "engine/pcg/procmodel/generator/model_context.h"
#include "engine/pcg/procmodel/generator/resolved_model.h"

#include "engine/math/noise/perlin.h"

namespace ProcModel {

//
// BARR'S DEFORMATIONS
//
void ApplyTaper(Geometry::MeshData& mesh, float factor, float y_min,
                float y_max) {
  // Barr 1984 Equation 2.2a (axis-of-variation = Y in this implementation)
  if (glm::abs(factor - 1.0f) < 1e-6f)
    return;  // no-op
  if (factor < 1e-4f) {
    Logger::getInstance().Log(
        LogLevel::Warning,
        "[ApplyTaper] factor near zero — singularity per Barr 1984; clamping");
    factor = 1e-4f;
  }

  const float y_span = y_max - y_min;
  if (y_span < 1e-6f)
    return;

  // f(y) = 1 + (factor - 1) * t,  t = clamp((y - y_min)/y_span, 0, 1)
  // f'(y) = (factor - 1) / y_span  inside the taper region, 0 outside
  const float slope = (factor - 1.0f) / y_span;

  for (auto& v : mesh.vertices) {
    const float t = glm::clamp((v.position.y - y_min) / y_span, 0.0f, 1.0f);
    const float r = 1.0f + (factor - 1.0f) * t;
    const bool inside = (v.position.y >= y_min) && (v.position.y <= y_max);
    const float fp = inside ? slope : 0.0f;

    // Jacobian (Barr Eq. 2.2b, Y-axis convention):
    //   J = [[r, fp*x, 0], [0, 1, 0], [0, fp*z, r]]
    // Normal transform uses J^-T. Computing J^-T directly for this sparse
    // structure: the inverse of an upper-triangular-ish 3x3 has closed form.
    //   J^-1 = [[1/r, -fp*x/r, 0], [0, 1, 0], [0, -fp*z/r, 1/r]]
    //   J^-T = transpose of the above.
    const float inv_r = 1.0f / r;
    const glm::vec3 n_old = v.normal;
    glm::vec3 n_new;
    n_new.x = inv_r * n_old.x;
    n_new.y = -fp * v.position.x * inv_r * n_old.x + n_old.y -
              fp * v.position.z * inv_r * n_old.z;
    n_new.z = inv_r * n_old.z;

    // Apply position transform
    v.position.x = r * v.position.x;
    v.position.z = r * v.position.z;
    // v.position.y unchanged

    if (glm::length(n_new) > 1e-6f)
      v.normal = glm::normalize(n_new);
  }
}

void ApplyTwist(Geometry::MeshData& mesh, float total_angle, float y_min,
                float y_max) {
  // Barr 1984 Equation 2.3a (axis-of-variation = Y in this implementation)
  if (glm::abs(total_angle) < 1e-6f)
    return;

  const float y_span = y_max - y_min;
  if (y_span < 1e-6f)
    return;

  const float twist_rate = total_angle / y_span;  // θ'(y), Barr's f'(z)

  for (auto& v : mesh.vertices) {
    const float t = glm::clamp((v.position.y - y_min) / y_span, 0.0f, 1.0f);
    const float theta = total_angle * t;
    const float c = std::cos(theta);
    const float s = std::sin(theta);
    const bool inside = (v.position.y >= y_min) && (v.position.y <= y_max);
    const float fp = inside ? twist_rate : 0.0f;

    const float x_old = v.position.x;
    const float z_old = v.position.z;

    // J^-T applied to old normal (Y-axis convention):
    const glm::vec3 n_old = v.normal;
    glm::vec3 n_new;
    n_new.x = c * n_old.x - s * n_old.z;
    n_new.y = z_old * fp * n_old.x + n_old.y - x_old * fp * n_old.z;
    n_new.z = s * n_old.x + c * n_old.z;

    // Position transform (Y-axis convention):
    v.position.x = c * x_old - s * z_old;
    v.position.z = s * x_old + c * z_old;
    // v.position.y unchanged.

    if (glm::length(n_new) > 1e-6f)
      v.normal = glm::normalize(n_new);
  }
}

void ApplyBend(Geometry::MeshData& mesh, float k, float y_0, float y_min,
               float y_max) {
  // Barr 1984 Equation 2.4a (piecewise linear bend along y-axis
  // centerline; deformation in the y-z plane)
  if (glm::abs(k) < 1e-6f)
    return;  // no-op

  const float inv_k = 1.0f / k;

  for (auto& v : mesh.vertices) {
    const float y = v.position.y;
    const float z = v.position.z;

    // ŷ clamps y into [y_min, y_max] per Barr.
    const float y_hat = glm::clamp(y, y_min, y_max);
    const float theta = k * (y_hat - y_0);
    const float C = std::cos(theta);
    const float S = std::sin(theta);

    // k̂ = k inside the bent region, 0 outside (rigid-body extension).
    const bool inside = (y >= y_min) && (y <= y_max);
    const float k_hat = inside ? k : 0.0f;

    // Position transform (piecewise on y):
    const float new_X = v.position.x;
    float new_Y = -S * (z - inv_k) + y_0;
    float new_Z = C * (z - inv_k) + inv_k;
    if (y < y_min) {
      new_Y += C * (y - y_min);
      new_Z += S * (y - y_min);
    } else if (y > y_max) {
      new_Y += C * (y - y_max);
      new_Z += S * (y - y_max);
    }

    // J^-T (Barr Eq. 2.4, dropping the scalar (1 - k̂z) prefactor since only
    // the direction of the resulting normal matters; renormalize after):
    //   row 0: (1 - k̂z, 0, 0)
    //   row 1: (0, C_θ, -S_θ(1 - k̂z))     -- but see note
    //   row 2: (0, S_θ, C_θ(1 - k̂z))
    //
    // The (1 - k̂z) factor multiplies only the columns originally scaled by
    // J's bend coupling; without dividing it out, the resulting normal is
    // (1 - k̂z) times the true normal — same direction. Safe to use raw.
    const float one_minus_kz = 1.0f - k_hat * z;
    const glm::vec3 n_old = v.normal;
    glm::vec3 n_new;
    n_new.x = one_minus_kz * n_old.x;
    n_new.y = C * n_old.y - S * one_minus_kz * n_old.z;
    n_new.z = S * n_old.y + C * one_minus_kz * n_old.z;

    v.position.x = new_X;
    v.position.y = new_Y;
    v.position.z = new_Z;

    if (glm::length(n_new) > 1e-6f)
      v.normal = glm::normalize(n_new);
  }
}

// Recompute flat-shaded normals by averaging triangle face normals per vertex
// Used as a fallback when J^-T transformation is impractical for a kernel
static void RecomputeNormals(Geometry::MeshData& mesh) {
  for (auto& v : mesh.vertices)
    v.normal = glm::vec3(0.0f);

  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    auto& v0 = mesh.vertices[mesh.indices[i]];
    auto& v1 = mesh.vertices[mesh.indices[i + 1]];
    auto& v2 = mesh.vertices[mesh.indices[i + 2]];
    const glm::vec3 face_normal =
        glm::cross(v1.position - v0.position, v2.position - v0.position);
    v0.normal += face_normal;
    v1.normal += face_normal;
    v2.normal += face_normal;
  }
  for (auto& v : mesh.vertices) {
    if (glm::length(v.normal) > 1e-6f)
      v.normal = glm::normalize(v.normal);
  }
}

void ApplyPerlinNoise(Geometry::MeshData& mesh, float frequency,
                      float amplitude, uint32_t seed) {
  // Perlin 1985 "An Image Synthesizer". Per-vertex offset along the vertex
  // normal, sampled from 3D coherent noise. Spatial coherence preserves
  // visual consistency (nearby vertices receive similar offsets).
  if (amplitude < 1e-6f || frequency < 1e-6f)
    return;

  // Mix seed into a deterministic per-axis offset so noise patterns differ
  // between instances even at identical positions.
  const float seed_offset = static_cast<float>(seed & 0xFFFF) * 0.0173f;

  for (auto& v : mesh.vertices) {
    const float n = Math::Perlin3D(v.position.x * frequency + seed_offset,
                                   v.position.y * frequency + seed_offset,
                                   v.position.z * frequency + seed_offset);
    v.position += v.normal * (n * amplitude);
  }

  // Normals are stale after positional offset. Recompute flat-shaded normals
  // by averaging triangle face normals per vertex.
  RecomputeNormals(mesh);
}

static DeformAxis ParseAxis(const nlohmann::json& j, const std::string& key,
                            DeformAxis fallback) {
  if (!j.contains(key))
    return fallback;
  const std::string s = j[key].get<std::string>();
  if (s == "x" || s == "X")
    return DeformAxis::X;
  if (s == "z" || s == "Z")
    return DeformAxis::Z;
  if (s == "y" || s == "Y")
    return DeformAxis::Y;
  Logger::getInstance().Log(
      LogLevel::Warning,
      "[VertexDeform] unrecognized axis '" + s + "', defaulting to Y");
  return fallback;
}

// Compute AABB along Y for the source mesh
static void ComputeYBounds(const Geometry::MeshData& mesh, float& y_min,
                           float& y_max) {
  y_min = std::numeric_limits<float>::max();
  y_max = std::numeric_limits<float>::lowest();
  for (const auto& v : mesh.vertices) {
    y_min = glm::min(y_min, v.position.y);
    y_max = glm::max(y_max, v.position.y);
  }
}

// Basis change: rotates mesh so chosen axis aligns with +Y,
// returning inverse rotation needed to restore original frame
// Identity if axis == Y. Standard rotation matrix
static glm::mat3 BasisToY(DeformAxis axis) {
  switch (axis) {
    case DeformAxis::X:
      // +X -> +Y (90° rotation about Z)
      return glm::mat3(glm::vec3(0.0f, 1.0f, 0.0f),
                       glm::vec3(-1.0f, 0.0f, 0.0f),
                       glm::vec3(0.0f, 0.0f, 1.0f));
    case DeformAxis::Z:
      // +Z -> +Y (-90° rotation about X)
      return glm::mat3(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
                       glm::vec3(0.0f, -1.0f, 0.0f));
    case DeformAxis::Y:
    default:
      return glm::mat3(1.0f);
  }
}

// Apply a basis matrix to every vertex position and normal in place
static void ApplyBasis(Geometry::MeshData& mesh, const glm::mat3& R) {
  for (auto& v : mesh.vertices) {
    v.position = R * v.position;
    v.normal = glm::normalize(R * v.normal);
  }
}

//
// PER-PART OPERATION
//
static std::shared_ptr<PCG::OperationData> ParsePartDeform(
    const nlohmann::json& params) {
  auto data = std::make_shared<PartDeformData>();

  data->apply_taper = params.value("apply_taper", false);
  data->apply_twist = params.value("apply_twist", false);
  data->apply_bend = params.value("apply_bend", false);
  data->apply_noise = params.value("apply_noise", false);

  data->taper_axis = ParseAxis(params, "taper_axis", DeformAxis::Y);
  data->twist_axis = ParseAxis(params, "twist_axis", DeformAxis::Y);
  data->bend_axis = ParseAxis(params, "bend_axis", DeformAxis::Y);

  data->noise_frequency = params.value("noise_frequency", 1.0f);

  if (!data->apply_taper && !data->apply_twist && !data->apply_bend &&
      !data->apply_noise) {
    Logger::getInstance().Log(
        LogLevel::Warning, "[Deform] no operations enabled — entry is a no-op");
    return nullptr;
  }

  return data;
}

// Looks up DeformationRange annotation for given group on graph node
// Return nullptr if group has no deformation range authored
static const DeformationRange* FindDeformationRange(
    const std::string& group_id, const ModelDescriptor& descriptor) {
  for (const auto& range : descriptor.part_deformation_ranges) {
    if (range.group_id == group_id)
      return &range;
  }
  if (descriptor.model_deformation_range)
    return &(*descriptor.model_deformation_range);
  return nullptr;
}

// Samples a single float from an optional [min,max] pair
static std::optional<float> SampleRange(const std::optional<float>& lo,
                                        const std::optional<float>& hi,
                                        const std::string& op_kind,
                                        ParameterSampler& sampler, pcg32& rng) {
  if (!lo || !hi)
    return std::nullopt;
  return sampler.Uniform(op_kind, "", *lo, *hi, rng);
}

static void ApplyPartDeform(const PCG::OperationData& base_data,
                            PCG::OperationContext& base_ctx) {
  const auto& data = PCG::OperationCast<PartDeformData>(base_data);
  auto& ctx = PCG::OperationCast<ModelContext>(base_ctx);

  const auto& source_mesh_data = ctx.graph.mesh_data;

  for (auto& d : ctx.model.descriptors) {
    // Look up the part's DeformationRange annotation via graph node.
    // Find deformation range matching this part's group, falling back
    // to model-wide range if no group-specific override exists.
    const DeformationRange* range =
        FindDeformationRange(d.group_id, ctx.descriptor);
    if (!range)
      continue;  // no deformation authored for this part

    ctx.sampler.Set(d.descriptor_id);

    // Sample per-part deformation values from the resolved range above.
    // Each part instance gets independent draws so identical-mesh parts
    // bend/twist/taper differently. Draws are attributed to the current
    // descriptor via the sampler cursor set above.
    std::optional<float> taper_factor;
    std::optional<float> twist_angle;
    std::optional<float> bend_angle;
    std::optional<float> noise_amplitude;

    if (data.apply_taper) {
      taper_factor =
          SampleRange(range->taper_factor_min, range->taper_factor_max, "taper",
                      ctx.sampler, ctx.rng);
    }
    if (data.apply_twist) {
      twist_angle = SampleRange(range->twist_angle_min, range->twist_angle_max,
                                "twist", ctx.sampler, ctx.rng);
    }
    if (data.apply_bend) {
      bend_angle = SampleRange(range->bend_angle_min, range->bend_angle_max,
                               "bend", ctx.sampler, ctx.rng);
    }
    if (data.apply_noise) {
      noise_amplitude =
          SampleRange(range->noise_amplitude_min, range->noise_amplitude_max,
                      "noise", ctx.sampler, ctx.rng);
    }

    InstanceGeometry instance_geometry;
    instance_geometry.descriptor_id = d.descriptor_id;
    instance_geometry.mesh_data.reserve(d.mesh_indices.size());

    for (int mesh_idx : d.mesh_indices) {
      if (mesh_idx < 0 || mesh_idx >= static_cast<int>(source_mesh_data.size()))
        continue;

      Geometry::MeshData working = source_mesh_data[mesh_idx];

      if (taper_factor) {
        const glm::mat3 R = BasisToY(data.taper_axis);
        const glm::mat3 R_inv =
            glm::transpose(R);  // orthonormal: transpose == inverse
        ApplyBasis(working, R);
        float y_min, y_max;
        ComputeYBounds(working, y_min, y_max);
        ApplyTaper(working, *taper_factor, y_min, y_max);
        ApplyBasis(working, R_inv);
      }

      if (twist_angle) {
        const glm::mat3 R = BasisToY(data.twist_axis);
        const glm::mat3 R_inv = glm::transpose(R);
        ApplyBasis(working, R);
        float y_min, y_max;
        ComputeYBounds(working, y_min, y_max);
        ApplyTwist(working, *twist_angle, y_min, y_max);
        ApplyBasis(working, R_inv);
      }

      if (bend_angle) {
        const glm::mat3 R = BasisToY(data.bend_axis);
        const glm::mat3 R_inv = glm::transpose(R);
        ApplyBasis(working, R);

        // Random azimuth around Y so different part instances bend in
        // different compass directions. Attributed to the current
        // descriptor via the sampler cursor set above.
        const float azimuth = ctx.sampler.Uniform(
            "bend_azimuth", "", 0.0f, glm::two_pi<float>(), ctx.rng);
        const float ca = std::cos(azimuth);
        const float sa = std::sin(azimuth);
        const glm::mat3 Yaw(glm::vec3(ca, 0.0f, -sa),
                            glm::vec3(0.0f, 1.0f, 0.0f),
                            glm::vec3(sa, 0.0f, ca));
        const glm::mat3 Yaw_inv = glm::transpose(Yaw);
        ApplyBasis(working, Yaw);

        float y_min, y_max;
        ComputeYBounds(working, y_min, y_max);
        // Convert total bend angle to Barr's k (rad per unit length).
        // y_0 placed at base (y_min): bend pivots around the bottom of the
        // part.
        const float y_span = y_max - y_min;
        const float k = (y_span > 1e-6f) ? (*bend_angle / y_span) : 0.0f;
        const float y_0 = y_min;
        ApplyBend(working, k, y_0, y_min, y_max);

        ApplyBasis(working, Yaw_inv);
        ApplyBasis(working, R_inv);
      }

      if (noise_amplitude) {
        // Seed derived from RNG so noise is deterministic for the resolved
        // seed.
        const uint32_t seed = ctx.rng();
        ApplyPerlinNoise(working, data.noise_frequency, *noise_amplitude, seed);
      }

      instance_geometry.mesh_data.push_back(std::move(working));
    }
    ctx.instance_geometry.push_back(std::move(instance_geometry));
  }
}

//
// INSTANCE-WIDE OPERATION
//
static std::shared_ptr<PCG::OperationData> ParseInstanceDeform(
    const nlohmann::json& params) {
  auto data = std::make_shared<InstanceDeformData>();

  data->apply_taper = params.value("apply_taper", false);
  data->apply_twist = params.value("apply_twist", false);
  data->apply_bend = params.value("apply_bend", false);

  data->taper_axis = ParseAxis(params, "taper_axis", DeformAxis::Y);
  data->twist_axis = ParseAxis(params, "twist_axis", DeformAxis::Y);
  data->bend_axis = ParseAxis(params, "bend_axis", DeformAxis::Y);

  if (!data->apply_taper && !data->apply_twist && !data->apply_bend) {
    Logger::getInstance().Log(
        LogLevel::Warning,
        "[InstanceDeform] no operations enabled — entry is a no-op");
    return nullptr;
  }

  return data;
}

// Find an existing instance_geometry entry for the given descriptor_id.
// Returns nullptr if no per-part pass produced geometry for this descriptor.
static InstanceGeometry* FindInstanceGeometry(
    std::vector<InstanceGeometry>& entries, const std::string& descriptor_id) {
  for (auto& entry : entries) {
    if (entry.descriptor_id == descriptor_id)
      return &entry;
  }
  return nullptr;
}

// Used by ApplyInstanceDeform, instance bucket
static std::optional<float> SampleRangeInstance(const std::optional<float>& lo,
                                                const std::optional<float>& hi,
                                                const std::string& op_kind,
                                                ParameterSampler& sampler,
                                                pcg32& rng) {
  if (!lo || !hi)
    return std::nullopt;
  return sampler.UniformInstance(op_kind, "", *lo, *hi, rng);
}

static void ApplyInstanceDeform(const PCG::OperationData& base_data,
                                PCG::OperationContext& base_ctx) {
  const auto& data = PCG::OperationCast<InstanceDeformData>(base_data);
  auto& ctx = PCG::OperationCast<ModelContext>(base_ctx);

  const auto& source_mesh_data = ctx.graph.mesh_data;

  // First pass: ensure every descriptor has an instance_geometry entry,
  // populating from source mesh data for parts that the per-part stage
  // didn't touch. This lets the model-wide pass operate uniformly over
  // a complete set of working meshes regardless of prior pipeline state.
  for (const auto& d : ctx.model.descriptors) {
    if (FindInstanceGeometry(ctx.instance_geometry, d.descriptor_id))
      continue;  // per-part stage already produced geometry for this descriptor

    InstanceGeometry entry;
    entry.descriptor_id = d.descriptor_id;
    entry.mesh_data.reserve(d.mesh_indices.size());

    for (int mesh_idx : d.mesh_indices) {
      if (mesh_idx < 0 || mesh_idx >= static_cast<int>(source_mesh_data.size()))
        continue;
      entry.mesh_data.push_back(source_mesh_data[mesh_idx]);  // copy
    }
    ctx.instance_geometry.push_back(std::move(entry));
  }

  // The model-wide deformation operates in world space: each descriptor's
  // mesh data is in its part-local frame, but the bend/twist/taper of the
  // whole instance should follow the assembled model's geometry. We need
  // to transform vertices into world space before computing the AABB and
  // applying the kernel, then transform back to local space when writing
  // results.
  //
  // Build per-descriptor world transforms from the ResolvedDescriptor's
  // local_transform (which DescriptorResolver populated with the node's
  // world_transform — see model_generator.cpp). This is the same matrix
  // ModelInstantiator uses to place the part in the scene.
  struct WorkingMesh {  // ! rename?
    InstanceGeometry* entry;
    size_t mesh_slot;
    glm::mat4 to_world;
    glm::mat4 to_local;
  };

  std::vector<WorkingMesh> working;
  for (const auto& d : ctx.model.descriptors) {
    InstanceGeometry* entry =
        FindInstanceGeometry(ctx.instance_geometry, d.descriptor_id);
    if (!entry)
      continue;
    const glm::mat4 to_world = d.local_transform;
    const glm::mat4 to_local = glm::inverse(to_world);
    for (size_t i = 0; i < entry->mesh_data.size(); ++i) {
      working.push_back({entry, i, to_world, to_local});
    }
  }

  if (working.empty())
    return;

  // Sample whole-instance parameters from model_deformation_range.
  std::optional<float> taper_factor;
  std::optional<float> twist_angle;
  std::optional<float> bend_angle;

  if (ctx.descriptor.model_deformation_range) {
    const auto& mdr = *ctx.descriptor.model_deformation_range;
    if (data.apply_taper) {
      taper_factor =
          SampleRangeInstance(mdr.taper_factor_min, mdr.taper_factor_max,
                              "taper", ctx.sampler, ctx.rng);
    }
    if (data.apply_twist) {
      twist_angle =
          SampleRangeInstance(mdr.twist_angle_min, mdr.twist_angle_max, "twist",
                              ctx.sampler, ctx.rng);
    }
    if (data.apply_bend) {
      bend_angle = SampleRangeInstance(mdr.bend_angle_min, mdr.bend_angle_max,
                                       "bend", ctx.sampler, ctx.rng);
    }
  }

  // Transform every working mesh into world space (in place).
  for (auto& wm : working) {
    for (auto& v : wm.entry->mesh_data[wm.mesh_slot].vertices) {
      const glm::vec4 world_pos = wm.to_world * glm::vec4(v.position, 1.0f);
      const glm::vec4 world_nrm = wm.to_world * glm::vec4(v.normal, 0.0f);
      v.position = glm::vec3(world_pos);
      v.normal = glm::normalize(glm::vec3(world_nrm));
    }
  }

  // Helper: compute Y-bounds across all working meshes after a basis change.
  auto compute_global_bounds = [&](const glm::mat3& R, float& y_min,
                                   float& y_max) {
    y_min = std::numeric_limits<float>::max();
    y_max = std::numeric_limits<float>::lowest();
    for (const auto& wm : working) {
      for (const auto& v : wm.entry->mesh_data[wm.mesh_slot].vertices) {
        const glm::vec3 p = R * v.position;
        y_min = glm::min(y_min, p.y);
        y_max = glm::max(y_max, p.y);
      }
    }
  };

  // Helper: apply a basis matrix to every vertex of every working mesh.
  auto apply_basis_all = [&](const glm::mat3& R) {
    for (auto& wm : working) {
      ApplyBasis(wm.entry->mesh_data[wm.mesh_slot], R);
    }
  };

  //
  // TAPER
  //
  if (taper_factor) {
    const glm::mat3 R = BasisToY(data.taper_axis);
    const glm::mat3 R_inv = glm::transpose(R);
    apply_basis_all(R);
    float y_min, y_max;
    compute_global_bounds(glm::mat3(1.0f), y_min, y_max);
    for (auto& wm : working) {
      ApplyTaper(wm.entry->mesh_data[wm.mesh_slot], *taper_factor, y_min,
                 y_max);
    }
    apply_basis_all(R_inv);
  }

  //
  // TWIST
  //
  if (twist_angle) {
    const glm::mat3 R = BasisToY(data.twist_axis);
    const glm::mat3 R_inv = glm::transpose(R);
    apply_basis_all(R);
    float y_min, y_max;
    compute_global_bounds(glm::mat3(1.0f), y_min, y_max);
    for (auto& wm : working) {
      ApplyTwist(wm.entry->mesh_data[wm.mesh_slot], *twist_angle, y_min, y_max);
    }
    apply_basis_all(R_inv);
  }

  //
  // BEND
  //
  if (bend_angle) {
    const glm::mat3 R = BasisToY(data.bend_axis);
    const glm::mat3 R_inv = glm::transpose(R);
    apply_basis_all(R);

    // Random azimuth around Y so different instances bend in different
    // compass directions. Sampled per-instance and recorded.
    const float azimuth = ctx.sampler.UniformInstance(
        "bend_azimuth", "", 0.0f, glm::two_pi<float>(), ctx.rng);
    const float ca = std::cos(azimuth);
    const float sa = std::sin(azimuth);
    const glm::mat3 Yaw(glm::vec3(ca, 0.0f, -sa), glm::vec3(0.0f, 1.0f, 0.0f),
                        glm::vec3(sa, 0.0f, ca));
    const glm::mat3 Yaw_inv = glm::transpose(Yaw);
    apply_basis_all(Yaw);

    float y_min, y_max;
    compute_global_bounds(glm::mat3(1.0f), y_min, y_max);
    const float y_span = y_max - y_min;
    const float k = (y_span > 1e-6f) ? (*bend_angle / y_span) : 0.0f;
    const float y_0 = y_min;
    for (auto& wm : working) {
      ApplyBend(wm.entry->mesh_data[wm.mesh_slot], k, y_0, y_min, y_max);
    }

    apply_basis_all(Yaw_inv);
    apply_basis_all(R_inv);
  }

  // Transform every working mesh back into part-local space.
  for (auto& wm : working) {
    for (auto& v : wm.entry->mesh_data[wm.mesh_slot].vertices) {
      const glm::vec4 local_pos = wm.to_local * glm::vec4(v.position, 1.0f);
      const glm::vec4 local_nrm = wm.to_local * glm::vec4(v.normal, 0.0f);
      v.position = glm::vec3(local_pos);
      v.normal = glm::normalize(glm::vec3(local_nrm));
    }
  }
}

//
// REGISTER OPERATIONS
//
void RegisterVertexDeformOperation(PCG::OperationRegistry& registry) {
  registry.Register("per_part_deform", &ParsePartDeform, &ApplyPartDeform);
  registry.Register("instance_deform", &ParseInstanceDeform,
                    &ApplyInstanceDeform);
}

}  // namespace ProcModel