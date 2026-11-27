#include "spawner_system.h"

#include <chrono>
#include <memory>

#include "engine/context/engine_context.h"
#include "engine/ecs/components/activation_component.h"
#include "engine/ecs/components/procedural_instance_component.h"
#include "engine/pcg/generator_profile.h"
#include "engine/pcg/generator_resource.h"
#include "engine/pcg/pcg_engine.h"
#include "engine/pcg/points/spawner_generator.h"
#include "engine/pcg/procworld/generator_settings.h"

namespace PCG {

void SpawnerSystem::Update() {
  auto& ecs = ECS::Main();
  auto& resources = EngineContext::resourceManager();
  const auto spawner_of = [&](Entity volume) -> SpawnerGenerator* {
    if (!ecs.Reg().valid(volume) || !ecs.Has<VolumeComponent>(volume))
      return nullptr;
    auto resource = resources.GetResourceAs<GeneratorResource>(
        ecs.Get<VolumeComponent>(volume).generator_id);
    return resource && resource->Get() &&
                   resource->Get()->GetType() == GeneratorType::Spawner
               ? static_cast<SpawnerGenerator*>(resource->Get())
               : nullptr;
  };

  // Volumes destroyed or given another generator since
  std::erase_if(volumes_, [&](auto& entry) {
    if (spawner_of(entry.first))
      return false;
    Clear(entry.second);
    return true;
  });

  for (auto [entity, volume] : ecs.View<VolumeComponent>().each()) {
    SpawnerGenerator* spawner = spawner_of(entity);
    if (!spawner)
      continue;
    State& state = volumes_[entity];
    // A dirty one is made again from its settings now; a build under way
    // finishes unread
    // TODO: replacing a build under way waits for it here (a std::async
    // future blocks as it goes); hand it to the job system instead
    if (volume.dirty) {
      volume.dirty = false;
      Clear(state);
      spawner->error.clear();
      spawner->points.clear();
      spawner->spawned = 0;
      const std::filesystem::path root =
          EngineContext::PCG().GetProcWorld().Root();
      state.building = std::async(
          std::launch::async,
          [source = std::shared_ptr<PointSource>(spawner->source->Clone()),
           profile = spawner->template_profile, root] {
            const auto start = std::chrono::steady_clock::now();
            Build build;
            if (source->Build(root, build.points, build.error)) {
              build.profile = LoadProfile(root / profile);
              if (profile.empty())
                build.error = "Choose a template profile";
              else if (build.profile.is_null())
                build.error = "Can't read the template profile " + profile;
            }
            build.ms = std::chrono::duration<float, std::milli>(
                           std::chrono::steady_clock::now() - start)
                           .count();
            return build;
          });
    }
    if (state.building.valid() &&
        state.building.wait_for(std::chrono::seconds(0)) ==
            std::future_status::ready) {
      const Build build = state.building.get();
      spawner->points = build.points;
      spawner->error = build.error;
      spawner->build_ms = build.ms;
      if (build.error.empty())
        Spawn(entity, *spawner, build, state);
    }
    // Its activation settings change without a respawn
    for (const Spawned& spawned : state.spawned)
      if (ecs.Reg().valid(spawned.entity)) {
        auto& activation = ecs.Get<ActivationComponent>(spawned.entity);
        activation.distance = spawner->activation_distance;
        activation.max_active = spawner->max_active;
      }
  }
}

void SpawnerSystem::Spawn(Entity volume, SpawnerGenerator& spawner,
                          const Build& build, State& state) {
  auto& ecs = ECS::Main();
  const std::filesystem::path root =
      EngineContext::PCG().GetProcWorld().Root();
  const std::string folder =
      spawner.edits_folder.empty()
          ? "edits/" + ecs.Get<TransformComponent>(volume).name_
          : spawner.edits_folder;
  for (const Point& point : build.points) {
    auto generator = ProcWorld::CreateGenerator(
        SpawnSettings(build.profile, point, spawner.bindings));
    if (!generator) {
      spawner.error = "The template profile " + spawner.template_profile +
                      " isn't a generator's";
      return;
    }
    auto [id, resource] =
        EngineContext::resourceManager().Create<GeneratorResource>(
            GetGeneratorTypeName(generator->GetType()));
    resource->SetGenerator(std::move(generator));
    resource->profile_ = spawner.template_profile;
    // Its hand edits, when it has any yet
    resource->edits_ = folder + "/" + point.name + ".edits";
    if (std::filesystem::exists(root / resource->edits_)) {
      auto layer = std::make_shared<EditLayer>();
      LoadEditLayer(root / resource->edits_, *layer);
      resource->edit_layer_ = std::move(layer);
    }

    auto [entity, transform] = ecs.CreateEntity(point.name, volume);
    transform.position_ = point.position;
    ecs.Add<ProceduralInstanceComponent>(entity, volume);
    ecs.Add<ActivationComponent>(entity);  // set each update
    auto& spawned = ecs.Add<VolumeComponent>(entity);
    spawned.generator_id = id;
    spawned.dirty = true;
    state.spawned.push_back({entity, id});
  }
  spawner.spawned = state.spawned.size();
}

void SpawnerSystem::Clear(State& state) {
  auto& ecs = ECS::Main();
  for (const Spawned& spawned : state.spawned) {
    if (ecs.Reg().valid(spawned.entity))
      ecs.DestroyEntity(spawned.entity);
    EngineContext::resourceManager().Release(spawned.generator);
  }
  state.spawned.clear();
}

void SpawnerSystem::Shutdown() {
  for (auto& [volume, state] : volumes_)
    Clear(state);
  volumes_.clear();
}

}  // namespace PCG
