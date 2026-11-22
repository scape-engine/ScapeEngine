#include "craft_instancer.h"

#include <cstring>

#include <glm/gtc/quaternion.hpp>

#include "engine/context/engine_context.h"
#include "engine/ecs/components/procedural_instance_component.h"
#include "engine/pcg/pcg_engine.h"
#include "engine/pcg/procworld/instancing/region_instancer.h"

#include "core/random.h"

namespace ProcWorld {

static constexpr const char* procmodel_prefix = "procmodel:";
// A craft's composited textures at most this a side: each craft has its own
// colours, so its own images
static constexpr int craft_texture_size = 512;

std::vector<CraftInstancer::CraftPlan> CraftInstancer::Plan(
    const Worldgen::CraftContent& content,
    const std::vector<Worldgen::FleetEntry>& fleet,
    const glm::vec3& bounds_min, const glm::vec3& bounds_max,
    const std::filesystem::path& root, size_t& missing) {
  std::vector<CraftPlan> plans;
  for (const Worldgen::FleetEntry& entry : fleet) {
    CraftPlan plan;
    plan.model = Worldgen::BuildCraft(content, entry.slot, entry.seed).model;
    plan.seed = entry.seed;
    if (plan.model.rfind(procmodel_prefix, 0) == 0) {
      plan.descriptor =
          (root / plan.model.substr(std::strlen(procmodel_prefix))).string();
      plan.archetype =
          EngineContext::PCG().GetProcModel().LoadedArchetype(plan.descriptor);
    }
    if (plan.model.empty()) {
      missing++;
      continue;
    }
    // Apart from the stream that picks the model and its parts
    Worldgen::Random r =
        Worldgen::Random::FromSeed(entry.seed ^ 0x9e3779b97f4a7c15ull, true);
    const glm::vec3 t(r.NextFloat(), r.NextFloat(), r.NextFloat());
    plan.position = glm::mix(bounds_min, bounds_max, t);
    plan.heading = r.NextFloat() * 360.0f;
    plans.push_back(std::move(plan));
  }
  return plans;
}

std::optional<ProcModel::InstanceData> CraftInstancer::Generate(
    const ProcModel::Subsystem& procmodel, const CraftPlan& plan) {
  if (!plan.archetype)
    return std::nullopt;
  return procmodel.Generate(*plan.archetype, plan.seed);
}

std::optional<ProcModel::Subsystem::TextureSet> CraftInstancer::PrepareTextures(
    ProcModel::Subsystem& procmodel, const Worldgen::CraftContent& content,
    const CraftPlan& plan, bool legacy_colours,
    ProcModel::Subsystem::TextureClaims& claims) {
  if (!plan.archetype)
    return std::nullopt;
  return procmodel.PrepareTextures(
      plan.descriptor, plan.archetype->GetDescriptor(),
      Worldgen::BuildCraftTextures(content, plan.seed, legacy_colours),
      craft_texture_size, &claims);
}

Entity CraftInstancer::Make(
    ECS& ecs, const CraftPlan& plan,
    const std::optional<ProcModel::InstanceData>& instance,
    std::optional<ProcModel::Subsystem::TextureSet> textures, Entity source,
    const std::filesystem::path& root) {
  Entity entity = entt::null;
  if (!plan.descriptor.empty()) {
    if (plan.archetype && instance) {
      auto& procmodel = EngineContext::PCG().GetProcModel();
      const std::vector<MaterialHandle>* materials =
          textures ? procmodel.ApplyTextures(plan.descriptor, *plan.archetype,
                                             std::move(*textures))
                   : nullptr;
      entity =
          procmodel.Instantiate(*instance, *plan.archetype, source, materials)
              .root;
    }
  } else {
    entity =
        RegionInstancer::CreateObject(ecs, plan.model, source, plan.seed, root);
  }
  if (entity == entt::null)
    return entity;
  auto& transform = ecs.Get<TransformComponent>(entity);
  transform.position_ = plan.position;
  transform.euler_angles_ = glm::vec3(0.0f, plan.heading, 0.0f);
  transform.rotation_ = glm::quat(glm::radians(transform.euler_angles_));
  transform.modified_ = true;
  ecs.Add<ProceduralInstanceComponent>(entity, source);
  return entity;
}

}  // namespace ProcWorld
