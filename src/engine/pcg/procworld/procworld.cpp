#include "procworld.h"

#include <chrono>
#include <fstream>
#include <sstream>

#include "engine/context/engine_context.h"
#include "engine/core/logger.h"
#include "engine/ecs/components/procedural_instance_component.h"
#include "engine/ecs/components/volume_component.h"
#include "engine/pcg/generator_resource.h"

#include "content_json.h"
#include "instancing/region_instancer.h"
#include "region_generator.h"

namespace ProcWorld {

void Subsystem::Init() {}

void Subsystem::Shutdown() {
  meshes_.clear();
}

void Subsystem::Update() {
  auto& ecs = ECS::Main();
  auto& resource_mgr = EngineContext::resourceManager();

  // Volumes destroyed since: their entities went with them
  std::erase_if(meshes_, [&](const auto& entry) {
    return !ecs.Reg().valid(entry.first);
  });

  for (auto [entity, volume] : ecs.View<VolumeComponent>().each()) {
    if (!volume.dirty)
      continue;
    auto generator =
        resource_mgr.GetResourceAs<PCG::GeneratorResource>(volume.generator_id);
    if (!generator || generator->GetGeneratorType() != PCG::GeneratorType::Region)
      continue;

    Build(entity, static_cast<RegionGenerator&>(*generator->Get()));
    volume.dirty = false;
  }
}

void Subsystem::Clear(Entity volume) {
  auto& ecs = ECS::Main();
  // Copy: destroying changes the children list
  const std::vector<Entity> children =
      ecs.Get<TransformComponent>(volume).children_;
  for (Entity child : children) {
    if (ecs.Has<ProceduralInstanceComponent>(child) &&
        ecs.Get<ProceduralInstanceComponent>(child).source_ == volume)
      ecs.DestroyEntity(child);
  }
  meshes_.erase(volume);
}

// TODO: build on a worker thread; the editor stalls while a region builds
// on the main thread
void Subsystem::Build(Entity volume, RegionGenerator& generator) {
  auto& ecs = ECS::Main();
  Clear(volume);
  generator.error.clear();
  generator.vertex_count = generator.instance_count = 0;

  const std::filesystem::path path = root_ / generator.content_path;
  std::ifstream file(path);
  if (generator.content_path.empty() || !file) {
    generator.error = "Can't open " + path.string();
    Logger::getInstance().Log(LogLevel::Error, "[ProcWorld] " + generator.error);
    return;
  }
  std::stringstream text;
  text << file.rdbuf();

  Worldgen::RegionContent content;
  if (!Worldgen::Json::LoadRegionContent(text.str(), content,
                                         &generator.error)) {
    Logger::getInstance().Log(LogLevel::Error, "[ProcWorld] " + path.string() +
                                                   ": " + generator.error);
    return;
  }

  if (terrain_material_ == INVALID_MATERIAL) {
    Content::MaterialData material;
    material.name = "Region Terrain";
    material.is_pbr = true;
    material.base_color = glm::vec4(0.45f, 0.42f, 0.36f, 1.0f);
    material.roughness = 0.9f;
    terrain_material_ = MaterialRegistry::Instance().Register(material);
  }

  glm::ivec3 cell = generator.cell;
  if (cell == glm::ivec3(0))
    cell = glm::ivec3(0, static_cast<int>(content.radius) + 16, 0);
  const int level = ecs.Get<VolumeComponent>(volume).lod_level;

  const auto start = std::chrono::steady_clock::now();
  const Worldgen::Region region =
      Worldgen::BuildRegion(content, cell.x, cell.y, cell.z, level);
  auto result = RegionInstancer::Instance(ecs, region, content, volume,
                                          terrain_material_, root_);
  generator.build_ms = std::chrono::duration<float, std::milli>(
                           std::chrono::steady_clock::now() - start)
                           .count();

  generator.vertex_count = region.mesh.size();
  generator.instance_count = result.object_entities.size();
  if (result.terrain_mesh)
    meshes_[volume] = std::move(result.terrain_mesh);

  Logger::getInstance().Log(
      LogLevel::Info, "[ProcWorld] " + generator.content_path + ": " +
                          std::to_string(generator.vertex_count) +
                          " vertices, " +
                          std::to_string(generator.instance_count) +
                          " objects, " +
                          std::to_string(generator.build_ms) + " ms");
}

}  // namespace ProcWorld
