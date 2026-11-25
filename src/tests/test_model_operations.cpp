// Checks must run in release builds too
#undef NDEBUG
#include <cassert>
#include <cfloat>
#include <cmath>
#include <iostream>
#include <set>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include "engine/pcg/procmodel/descriptor/model_descriptor_parser.h"
#include "engine/pcg/procmodel/generator/locator_context.h"
#include "engine/pcg/procmodel/generator/model_context.h"
#include "engine/pcg/procmodel/generator/model_generator.h"
#include "engine/pcg/procmodel/generator/operations.h"
#include "engine/pcg/procmodel/generator/operations/rotation_jitter.h"

namespace Tests {

namespace {

constexpr int sides = 16;
constexpr int rings = 9;

// A column along +y from 0 to 2, radius 0.5, smooth normals, wound outward
Geometry::MeshData _Column() {
  Geometry::MeshData mesh;
  for (int k = 0; k < rings; ++k)
    for (int i = 0; i < sides; ++i) {
      const float a = glm::two_pi<float>() * i / sides;
      const glm::vec3 n(std::cos(a), 0.0f, std::sin(a));
      mesh.vertices.emplace_back(n * 0.5f + glm::vec3(0, 2.0f * k / (rings - 1), 0),
                                 n, glm::vec2(0.0f), glm::vec3(0.0f),
                                 glm::vec3(0.0f));
    }
  for (int k = 0; k + 1 < rings; ++k)
    for (int i = 0; i < sides; ++i) {
      const uint32_t a = k * sides + i, b = k * sides + (i + 1) % sides;
      mesh.indices.insert(mesh.indices.end(),
                          {a, a + sides, b, b, a + sides, b + sides});
    }
  return mesh;
}

// The column turned so its +y runs along `axis` (0 x, 1 y, 2 z)
Geometry::MeshData _ColumnAlong(int axis) {
  Geometry::MeshData mesh = _Column();
  const glm::mat3 turn = axis == 0   ? glm::mat3(glm::vec3(0, -1, 0),
                                                 glm::vec3(1, 0, 0),
                                                 glm::vec3(0, 0, 1))
                         : axis == 2 ? glm::mat3(glm::vec3(1, 0, 0),
                                                 glm::vec3(0, 0, 1),
                                                 glm::vec3(0, -1, 0))
                                     : glm::mat3(1.0f);
  for (auto& v : mesh.vertices) {
    v.position = turn * v.position;
    v.normal = turn * v.normal;
  }
  return mesh;
}

glm::vec3 _RingCentre(const Geometry::MeshData& mesh, int k) {
  glm::vec3 sum(0.0f);
  for (int i = 0; i < sides; ++i)
    sum += mesh.vertices[k * sides + i].position;
  return sum / static_cast<float>(sides);
}

float _RingRadius(const Geometry::MeshData& mesh, int k) {
  const glm::vec3 centre = _RingCentre(mesh, k);
  float sum = 0.0f;
  for (int i = 0; i < sides; ++i)
    sum += glm::distance(mesh.vertices[k * sides + i].position, centre);
  return sum / sides;
}

// The smallest agreement of a vertex normal with its faces' normals
float _NormalAgreement(const Geometry::MeshData& mesh) {
  std::vector<glm::vec3> faces(mesh.vertices.size(), glm::vec3(0.0f));
  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    const glm::vec3& a = mesh.vertices[mesh.indices[i]].position;
    const glm::vec3 n =
        glm::cross(mesh.vertices[mesh.indices[i + 1]].position - a,
                   mesh.vertices[mesh.indices[i + 2]].position - a);
    for (int c = 0; c < 3; ++c)
      faces[mesh.indices[i + c]] += n;
  }
  float least = 1.0f;
  for (size_t i = 0; i < faces.size(); ++i)
    least = std::min(least, glm::dot(glm::normalize(faces[i]),
                                     glm::normalize(mesh.vertices[i].normal)));
  return least;
}

struct Model {
  ProcModel::ModelDescriptor descriptor;
  ProcModel::ModelGraph graph;
  ProcModel::InstanceModel model;
};

ProcModel::ResolvedDescriptor _Part(const std::string& id,
                                    const std::string& group,
                                    const glm::mat4& at = glm::mat4(1.0f)) {
  ProcModel::ResolvedDescriptor d;
  d.descriptor_id = id;
  d.group_id = group;
  d.mesh_indices = {0};
  d.local_transform = at;
  return d;
}

// Runs model operation `kind` with `params` on `model` from `seed`
std::vector<ProcModel::InstanceGeometry> _Run(
    const PCG::OperationRegistry& registry, const std::string& kind,
    const nlohmann::json& params, Model& model, uint64_t seed) {
  const auto data = registry.Parse(kind, params);
  assert(data);
  pcg32 rng(seed);
  ProcModel::ParameterSampler sampler;
  ProcModel::ModelContext ctx(model.descriptor, model.graph, model.model, rng,
                              sampler);
  assert(registry.Apply(kind, *data, ctx));
  return ctx.instance_geometry;
}

// Runs locator operation `kind` on `locator` placed on `activator`
glm::mat4 _RunLocator(const PCG::OperationRegistry& registry,
                      const std::string& kind, const nlohmann::json& params,
                      glm::mat4 locator, const glm::mat4& activator,
                      uint64_t seed) {
  const auto data = registry.Parse(kind, params);
  assert(data);
  pcg32 rng(seed);
  ProcModel::ParameterSampler sampler;
  const std::string id = "L";
  ProcModel::LocatorContext ctx(locator, activator, id, rng, sampler);
  assert(registry.Apply(kind, *data, ctx));
  return locator;
}

float _Angle(const glm::vec3& a, const glm::vec3& b) {
  return std::acos(glm::clamp(
      glm::dot(glm::normalize(a), glm::normalize(b)), -1.0f, 1.0f));
}

void _TestDuplicateInstances(const PCG::OperationRegistry& registry) {
  assert(registry.Parse("duplicate_instances", nlohmann::json::object()));
  assert(!registry.Parse("duplicate_instances", {{"min_count", 3}, {"max_count", 2}}));
  assert(!registry.Parse("duplicate_instances", {{"min_count", 0}}));

  const glm::mat4 at = glm::translate(glm::mat4(1.0f), glm::vec3(1, 2, 3));
  std::set<size_t> counts;
  for (uint64_t seed = 1; seed <= 40; ++seed) {
    Model run;
    run.model.descriptors = {_Part("TRUNK", "trunk"),
                             _Part("B1", "branches", at),
                             _Part("B2", "branches")};
    _Run(registry, "duplicate_instances",
         {{"target_group_id", "branches"}, {"min_count", 2}, {"max_count", 4}},
         run, seed);
    std::set<std::string> ids;
    size_t trunks = 0, b1 = 0;
    for (const auto& d : run.model.descriptors) {
      assert(ids.insert(d.descriptor_id).second);  // ids stay unique
      trunks += d.group_id == "trunk";
      if (d.descriptor_id.rfind("B1", 0) == 0) {
        ++b1;
        assert(d.local_transform == at);  // copies sit where the original does
      }
    }
    assert(trunks == 1);  // other groups are left alone
    assert(b1 >= 2 && b1 <= 4);  // the count includes the original
    counts.insert(b1);
  }
  assert(counts.count(2) && counts.count(4));

  // With a spread, each copy moves and turns within it in the part's own
  // frame; the original stays
  const glm::mat4 turned = glm::rotate(at, 1.0f, glm::vec3(0, 1, 0));
  Model spread;
  spread.model.descriptors = {_Part("B1", "branches", turned)};
  _Run(registry, "duplicate_instances",
       {{"min_count", 5},
        {"max_count", 5},
        {"offset", {0.5, 0.0, 0.5}},
        {"rotation", {0, 90, 0}}},
       spread, 3);
  assert(spread.model.descriptors.size() == 5 &&
         spread.model.descriptors[0].local_transform == turned);
  std::set<float> places;
  for (size_t i = 1; i < 5; ++i) {
    const glm::mat4 move =
        glm::inverse(turned) * spread.model.descriptors[i].local_transform;
    const glm::vec3 offset(move[3]);
    assert(std::abs(offset.x) <= 0.5f && offset.y == 0.0f &&
           std::abs(offset.z) <= 0.5f);
    // a turn about y alone, by at most 90 degrees
    assert(std::abs(move[1][1] - 1.0f) < 1e-5f);
    assert(_Angle(glm::vec3(move[0]), glm::vec3(1, 0, 0)) <=
           glm::half_pi<float>() + 1e-4f);
    places.insert(offset.x);
  }
  assert(places.size() == 4);  // no two copies alike
}

void _TestRadialAlign(const PCG::OperationRegistry& registry) {
  const glm::mat4 activator =
      glm::translate(glm::mat4(1.0f), glm::vec3(5, 0, 5));
  // A locator east of the activator, turned and at half scale
  const glm::mat4 locator =
      glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(6, 2, 5)),
                             0.7f, glm::vec3(1, 1, 0)),
                 glm::vec3(0.5f));
  const glm::mat4 out =
      _RunLocator(registry, "radial_align", {}, locator, activator, 1);
  assert(out[3] == locator[3]);  // stays where it is
  assert(_Angle(glm::vec3(out[2]), glm::vec3(1, 0, 0)) < 1e-4f);  // faces out
  assert(_Angle(glm::vec3(out[1]), glm::vec3(0, 1, 0)) < 1e-4f);  // up stays up
  assert(glm::determinant(glm::mat3(out)) > 0.0f);  // not mirrored
  // keeps its scale, which the parts placed on it take
  for (int c = 0; c < 3; ++c)
    assert(std::abs(glm::length(glm::vec3(out[c])) - 0.5f) < 1e-5f);

  // on the activator's axis there is no outward: left as it is
  const glm::mat4 above = glm::translate(glm::mat4(1.0f), glm::vec3(5, 3, 5));
  assert(_RunLocator(registry, "radial_align", {}, above, activator, 1) == above);
}

void _TestRotationJitter(const PCG::OperationRegistry& registry) {
  const auto shorthand = std::static_pointer_cast<ProcModel::RotationJitterData>(
      registry.Parse("rotation_jitter", {{"pitch", 10}}));
  assert(std::abs(shorthand->min.x + glm::radians(10.0f)) < 1e-6f &&
         std::abs(shorthand->max.x - glm::radians(10.0f)) < 1e-6f &&
         shorthand->min.y == 0.0f && shorthand->max.z == 0.0f);
  const auto explicit_range =
      std::static_pointer_cast<ProcModel::RotationJitterData>(
          registry.Parse("rotation_jitter",
                         {{"yaw_min", 5}, {"yaw_max", 15}}));
  assert(std::abs(explicit_range->min.y - glm::radians(5.0f)) < 1e-6f &&
         std::abs(explicit_range->max.y - glm::radians(15.0f)) < 1e-6f);

  const glm::mat4 locator = glm::rotate(
      glm::translate(glm::mat4(1.0f), glm::vec3(1, 2, 3)), 1.0f,
      glm::vec3(0, 0, 1));
  float widest = 0.0f;
  for (uint64_t seed = 1; seed <= 200; ++seed) {
    const glm::mat4 out = _RunLocator(registry, "rotation_jitter",
                                      {{"pitch", 10}}, locator, locator, seed);
    assert(out == _RunLocator(registry, "rotation_jitter", {{"pitch", 10}},
                              locator, locator, seed));
    assert(out[3] == locator[3]);
    // pitch turns about the locator's own x, by at most 10 degrees
    assert(_Angle(glm::vec3(out[0]), glm::vec3(locator[0])) < 1e-3f);
    const float angle = _Angle(glm::vec3(out[1]), glm::vec3(locator[1]));
    assert(angle <= glm::radians(10.0f) + 1e-4f);
    widest = std::max(widest, angle);
  }
  assert(widest > glm::radians(8.0f));
}

// Per-part deformations of the column with fixed amounts
Geometry::MeshData _Deformed(const PCG::OperationRegistry& registry,
                             const Geometry::MeshData& column,
                             const nlohmann::json& params,
                             const nlohmann::json& range, uint64_t seed = 1) {
  Model model;
  model.graph.mesh_data = {column};
  nlohmann::json trunk = range;
  trunk["group_id"] = "trunk";
  assert(ProcModel::ModelDescriptorParser::FromJson(
      {{"model_id", "column"}, {"part_deformation_ranges", {trunk}}},
      model.descriptor));
  model.model.descriptors = {_Part("TRUNK", "trunk"), _Part("LEAF", "leaves")};
  const auto geometry = _Run(registry, "per_part_deform", params, model, seed);
  // only the parts with a range get geometry of their own
  assert(geometry.size() == 1 && geometry[0].descriptor_id == "TRUNK" &&
         geometry[0].mesh_data.size() == 1);
  return geometry[0].mesh_data[0];
}

void _TestPartDeform(const PCG::OperationRegistry& registry) {
  assert(!registry.Parse("per_part_deform", nlohmann::json::object()));

  // Taper narrows the far end along each axis to the factor
  for (int axis = 0; axis < 3; ++axis) {
    const std::string name(1, "xyz"[axis]);
    const auto mesh = _Deformed(
        registry, _ColumnAlong(axis),
        {{"apply_taper", true}, {"taper_axis", name}},
        {{"taper_factor_min", 0.5}, {"taper_factor_max", 0.5}});
    assert(std::abs(_RingRadius(mesh, 0) - 0.5f) < 1e-4f);
    assert(std::abs(_RingRadius(mesh, rings - 1) - 0.25f) < 1e-4f);
    assert(_NormalAgreement(mesh) > 0.97f);
  }

  // Twist turns the top ring by the angle, keeping heights and radii
  {
    const Geometry::MeshData column = _Column();
    const auto mesh = _Deformed(registry, column, {{"apply_twist", true}},
                                {{"twist_angle_min", 90}, {"twist_angle_max", 90}});
    for (int i = 0; i < sides; ++i) {
      const auto& before = column.vertices[(rings - 1) * sides + i].position;
      const auto& after = mesh.vertices[(rings - 1) * sides + i].position;
      assert(std::abs(after.y - before.y) < 1e-5f);
      assert(std::abs(_Angle(glm::vec3(before.x, 0, before.z),
                             glm::vec3(after.x, 0, after.z)) -
                      glm::half_pi<float>()) < 1e-3f);
      assert(glm::distance(mesh.vertices[i].position,
                           column.vertices[i].position) < 1e-5f);
    }
    assert(_NormalAgreement(mesh) > 0.97f);
  }

  // Bend turns the top by the angle, keeping the column's length
  {
    const auto mesh = _Deformed(registry, _Column(), {{"apply_bend", true}},
                                {{"bend_angle_min", 60}, {"bend_angle_max", 60}});
    float length = 0.0f;
    for (int k = 1; k < rings; ++k)
      length += glm::distance(_RingCentre(mesh, k), _RingCentre(mesh, k - 1));
    assert(std::abs(length - 2.0f) < 0.02f);
    const glm::vec3 top = _RingCentre(mesh, rings - 1) - _RingCentre(mesh, rings - 2);
    assert(std::abs(_Angle(top, glm::vec3(0, 1, 0)) - glm::radians(60.0f)) <
           glm::radians(60.0f) / (rings - 1));  // one ring's share of the arc
    assert(glm::distance(_RingCentre(mesh, 0), glm::vec3(0.0f)) < 1e-4f);
    assert(_NormalAgreement(mesh) > 0.97f);
  }

  // Noise moves each vertex along its normal, by at most the amplitude
  {
    const Geometry::MeshData column = _Column();
    const auto mesh = _Deformed(
        registry, column, {{"apply_noise", true}, {"noise_frequency", 2.0}},
        {{"noise_amplitude_min", 0.1}, {"noise_amplitude_max", 0.1}});
    float moved = 0.0f;
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
      const glm::vec3 offset =
          mesh.vertices[i].position - column.vertices[i].position;
      assert(glm::length(offset) <= 0.1f + 1e-5f);
      assert(glm::length(glm::cross(offset, column.vertices[i].normal)) < 1e-5f);
      moved = std::max(moved, glm::length(offset));
    }
    assert(moved > 0.01f);
  }

  // The same seed deforms alike; other seeds and parts draw anew
  const nlohmann::json params = {{"apply_twist", true}};
  const nlohmann::json range = {{"twist_angle_min", -90}, {"twist_angle_max", 90}};
  const auto a = _Deformed(registry, _Column(), params, range, 7);
  const auto b = _Deformed(registry, _Column(), params, range, 7);
  const auto c = _Deformed(registry, _Column(), params, range, 8);
  const size_t top = (rings - 1) * sides;
  assert(a.vertices[top].position == b.vertices[top].position);
  assert(a.vertices[top].position != c.vertices[top].position);

  // A second entry builds on the first: two halving tapers quarter the top
  Model model;
  model.graph.mesh_data = {_Column()};
  assert(ProcModel::ModelDescriptorParser::FromJson(
      {{"model_id", "column"},
       {"model_deformation_range",
        {{"taper_factor_min", 0.5}, {"taper_factor_max", 0.5}}}},
      model.descriptor));
  model.model.descriptors = {_Part("TRUNK", "trunk")};
  const auto taper = registry.Parse("per_part_deform", {{"apply_taper", true}});
  pcg32 rng(1);
  ProcModel::ParameterSampler sampler;
  ProcModel::ModelContext ctx(model.descriptor, model.graph, model.model, rng,
                              sampler);
  assert(registry.Apply("per_part_deform", *taper, ctx) &&
         registry.Apply("per_part_deform", *taper, ctx));
  assert(ctx.instance_geometry.size() == 1);
  assert(std::abs(_RingRadius(ctx.instance_geometry[0].mesh_data[0], rings - 1) -
                  0.125f) < 1e-4f);
}

// A part's meshes deform as one shape, and the parts placed on it go where
// its deformation takes their places
void _TestPartDeformCarries(const PCG::OperationRegistry& registry) {
  // A trunk of two stacked columns, a branch on its top and a twig on the
  // branch's top
  Model model;
  Geometry::MeshData upper = _Column();
  for (auto& v : upper.vertices)
    v.position.y += 2.0f;
  model.graph.mesh_data = {_Column(), upper};
  assert(ProcModel::ModelDescriptorParser::FromJson(
      {{"model_id", "tree"},
       {"part_deformation_ranges",
        {{{"group_id", "trunk"},
          {"bend_angle_min", 60},
          {"bend_angle_max", 60},
          {"twist_angle_min", 90},
          {"twist_angle_max", 90}}}}},
      model.descriptor));
  auto trunk = _Part("TRUNK", "trunk");
  trunk.mesh_indices = {0, 1};
  auto branch = _Part("BRANCH", "branches",
                      glm::rotate(glm::translate(glm::mat4(1.0f),
                                                 glm::vec3(0.5f, 4, 0)),
                                  0.5f, glm::vec3(0, 0, 1)));
  branch.activator_id = "TRUNK";
  branch.applied_scale = glm::vec3(0.5f);
  auto twig = _Part("TWIG", "twigs",
                    glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 5, 0)));
  twig.activator_id = "BRANCH";
  model.model.descriptors = {trunk, branch, twig};
  const glm::mat4 branch_before = ProcModel::ModelGenerator::PartWorld(branch);
  const glm::mat4 twig_before = ProcModel::ModelGenerator::PartWorld(twig);

  const auto geometry = _Run(registry, "per_part_deform",
                             {{"apply_bend", true}, {"apply_twist", true}},
                             model, 1);
  assert(geometry.size() == 1 && geometry[0].mesh_data.size() == 2);
  const auto& lower_mesh = geometry[0].mesh_data[0];
  const auto& upper_mesh = geometry[0].mesh_data[1];
  // the joint stays shut
  for (int i = 0; i < sides; ++i)
    assert(glm::distance(lower_mesh.vertices[(rings - 1) * sides + i].position,
                         upper_mesh.vertices[i].position) < 1e-4f);
  // the whole trunk bends by the angle: its top turns 60 degrees
  const glm::vec3 top = _RingCentre(upper_mesh, rings - 1);
  const glm::vec3 tangent = top - _RingCentre(upper_mesh, rings - 2);
  assert(std::abs(_Angle(tangent, glm::vec3(0, 1, 0)) - glm::radians(60.0f)) <
         glm::radians(60.0f) / (2 * (rings - 1)));

  // The branch sits where its place on the surface went, turned with the
  // surface there, at its scale
  const glm::mat4 branch_after =
      ProcModel::ModelGenerator::PartWorld(model.model.descriptors[1]);
  for (int c = 0; c < 3; ++c)
    assert(std::abs(glm::length(glm::vec3(branch_after[c])) - 0.5f) < 1e-4f);
  // where a vertex beside its place went: the top ring's vertex at angle 0
  // (its place, (0.5, 4, 0), is that vertex before the deformation)
  const glm::vec3 vertex = upper_mesh.vertices[(rings - 1) * sides].position;
  assert(glm::distance(glm::vec3(branch_after[3]), vertex) < 1e-3f);
  assert(glm::length(glm::vec3(branch_after[3]) - glm::vec3(branch_before[3])) >
         0.5f);
  // its y leans with the trunk's top
  const glm::vec3 lean = glm::normalize(tangent);
  const float turned = _Angle(glm::vec3(branch_after[1]), lean);
  assert(std::abs(turned - _Angle(glm::vec3(branch_before[1]),
                                  glm::vec3(0, 1, 0))) < glm::radians(4.0f));
  // the twig moves with the branch, rigidly
  const glm::mat4 twig_after =
      ProcModel::ModelGenerator::PartWorld(model.model.descriptors[2]);
  const glm::mat4 carried =
      branch_after * glm::inverse(branch_before) * twig_before;
  for (int c = 0; c < 4; ++c)
    assert(glm::length(twig_after[c] - carried[c]) < 1e-4f);
}

// Ridges lobe the cross-section, and a twist winds them; wander moves the
// centre line sideways in changing directions, the base staying put
void _TestRidgesAndWander(const PCG::OperationRegistry& registry) {
  const Geometry::MeshData column = _Column();
  const auto ridged =
      _Deformed(registry, column, {{"apply_ridges", true}, {"ridge_count", 3}},
                {{"ridge_amplitude_min", 0.2}, {"ridge_amplitude_max", 0.2}});
  for (size_t i = 0; i < column.vertices.size(); ++i) {
    const glm::vec3& p = column.vertices[i].position;
    const float want =
        0.5f * (1.0f + 0.2f * std::cos(3.0f * std::atan2(p.z, p.x)));
    const glm::vec3& q = ridged.vertices[i].position;
    assert(std::abs(glm::length(glm::vec2(q.x, q.z)) - want) < 1e-5f);
    assert(q.y == p.y);
  }
  assert(_NormalAgreement(ridged) > 0.95f);

  // the ridge at angle 0 ends a quarter turn round at the top
  const auto wound = _Deformed(
      registry, column,
      {{"apply_ridges", true}, {"ridge_count", 3}, {"apply_twist", true}},
      {{"ridge_amplitude_min", 0.2},
       {"ridge_amplitude_max", 0.2},
       {"twist_angle_min", 90},
       {"twist_angle_max", 90}});
  const glm::vec3 top = wound.vertices[(rings - 1) * sides].position;
  assert(std::abs(glm::length(glm::vec2(top.x, top.z)) - 0.6f) < 1e-5f);
  assert(std::abs(std::abs(std::atan2(top.z, top.x)) - glm::half_pi<float>()) <
         1e-3f);

  // Wander: sideways only, none at the base, and turning as it goes
  bool turns = false;
  for (uint64_t seed = 1; seed <= 8; ++seed) {
    const auto wandered = _Deformed(
        registry, column, {{"apply_wander", true}, {"wander_waves", 2.0}},
        {{"wander_amplitude_min", 0.4}, {"wander_amplitude_max", 0.4}}, seed);
    float farthest = 0.0f;
    std::vector<glm::vec2> shifts;
    for (int k = 0; k < rings; ++k) {
      const glm::vec3 shift =
          _RingCentre(wandered, k) - _RingCentre(column, k);
      assert(std::abs(shift.y) < 1e-5f);
      shifts.emplace_back(shift.x, shift.z);
      farthest = std::max(farthest, glm::length(shifts.back()));
    }
    assert(glm::length(shifts[0]) < 1e-5f);
    assert(farthest <= 0.8f + 1e-4f && farthest > 0.01f);
    assert(_NormalAgreement(wandered) > 0.95f);
    // the steps between rings point different ways
    float widest = 0.0f;
    for (int k = 2; k < rings; ++k) {
      const glm::vec2 a = shifts[k - 1] - shifts[k - 2];
      const glm::vec2 b = shifts[k] - shifts[k - 1];
      if (glm::length(a) > 1e-3f && glm::length(b) > 1e-3f)
        widest = std::max(widest, std::acos(glm::clamp(
                                      glm::dot(glm::normalize(a),
                                               glm::normalize(b)),
                                      -1.0f, 1.0f)));
    }
    turns = turns || widest > glm::radians(45.0f);
  }
  assert(turns);

  // a part on top goes with the top
  Model model;
  model.graph.mesh_data = {column};
  assert(ProcModel::ModelDescriptorParser::FromJson(
      {{"model_id", "tree"},
       {"part_deformation_ranges",
        {{{"group_id", "trunk"},
          {"wander_amplitude_min", 0.4},
          {"wander_amplitude_max", 0.4}}}}},
      model.descriptor));
  auto top_part = _Part("TOP", "tops",
                        glm::translate(glm::mat4(1.0f), glm::vec3(0, 2, 0)));
  top_part.activator_id = "TRUNK";
  model.model.descriptors = {_Part("TRUNK", "trunk"), top_part};
  const auto geometry =
      _Run(registry, "per_part_deform", {{"apply_wander", true}}, model, 3);
  const glm::vec3 placed(
      ProcModel::ModelGenerator::PartWorld(model.model.descriptors[1])[3]);
  assert(glm::distance(placed,
                       _RingCentre(geometry[0].mesh_data[0], rings - 1)) <
         1e-4f);
}

void _TestInstanceDeform(const PCG::OperationRegistry& registry) {
  assert(!registry.Parse("instance_deform", nlohmann::json::object()));

  // A trunk and, on top of it, a branch with a sampled scale and turn (as a
  // locator placement gets them)
  Model model;
  model.graph.mesh_data = {_Column()};
  assert(ProcModel::ModelDescriptorParser::FromJson(
      {{"model_id", "column"},
       {"model_deformation_range",
        {{"twist_angle_min", 90}, {"twist_angle_max", 90}}}},
      model.descriptor));
  auto branch = _Part("BRANCH", "branches",
                      glm::translate(glm::mat4(1.0f), glm::vec3(0.3f, 2, 0)));
  branch.applied_scale = glm::vec3(0.5f);
  branch.applied_rotation = glm::vec3(0.0f, 0.0f, -0.6f);
  model.model.descriptors = {_Part("TRUNK", "trunk"), branch};

  const auto geometry =
      _Run(registry, "instance_deform", {{"apply_twist", true}}, model, 1);
  assert(geometry.size() == 2);

  // Every vertex, where the instantiator places it, twists about the model's
  // y by its height's share of the model's span
  float low = FLT_MAX, high = -FLT_MAX;
  for (const auto& d : model.model.descriptors)
    for (const auto& v : model.graph.mesh_data[0].vertices) {
      const float y =
          (ProcModel::ModelGenerator::PartWorld(d) * glm::vec4(v.position, 1.0f)).y;
      low = std::min(low, y);
      high = std::max(high, y);
    }
  for (size_t p = 0; p < 2; ++p) {
    const glm::mat4 world =
        ProcModel::ModelGenerator::PartWorld(model.model.descriptors[p]);
    assert(geometry[p].descriptor_id == model.model.descriptors[p].descriptor_id);
    const auto& source = model.graph.mesh_data[0].vertices;
    const auto& deformed = geometry[p].mesh_data[0].vertices;
    for (size_t i = 0; i < source.size(); ++i) {
      const glm::vec3 before = world * glm::vec4(source[i].position, 1.0f);
      const glm::vec3 after = world * glm::vec4(deformed[i].position, 1.0f);
      const float turn = glm::half_pi<float>() * (before.y - low) / (high - low);
      const glm::vec3 want(std::cos(turn) * before.x - std::sin(turn) * before.z,
                           before.y,
                           std::sin(turn) * before.x + std::cos(turn) * before.z);
      assert(glm::distance(after, want) < 1e-4f);
    }
  }
}

}  // namespace

void TestModelOperations() {
  PCG::OperationRegistry registry;
  ProcModel::RegisterModelOperations(registry);
  _TestDuplicateInstances(registry);
  _TestRadialAlign(registry);
  _TestRotationJitter(registry);
  _TestPartDeform(registry);
  _TestPartDeformCarries(registry);
  _TestRidgesAndWander(registry);
  _TestInstanceDeform(registry);
  std::cout << "Model operations: OK" << std::endl;
}

}  // namespace Tests
