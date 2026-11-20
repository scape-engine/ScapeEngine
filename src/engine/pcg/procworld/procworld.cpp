#include "procworld.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <sstream>

#include "engine/context/engine_context.h"
#include "engine/core/logger.h"
#include "engine/ecs/components/procedural_instance_component.h"
#include "engine/ecs/components/volume_component.h"
#include "engine/pcg/generator_resource.h"

#include "api/planet_grid.h"
#include "content_json.h"
#include "instancing/region_instancer.h"
#include "planet_generator.h"
#include "region_generator.h"

namespace ProcWorld {

using Clock = std::chrono::steady_clock;

static float _Milliseconds(Clock::time_point start) {
  return std::chrono::duration<float, std::milli>(Clock::now() - start)
      .count();
}

// Reads region content from a JSON file; false with `error` set on failure
static bool _LoadContent(const std::filesystem::path& root,
                         const std::string& relative,
                         Worldgen::RegionContent& content, std::string& error) {
  const std::filesystem::path path = root / relative;
  std::ifstream file(path);
  if (relative.empty() || !file) {
    error = "Can't open " + path.string();
    Logger::getInstance().Log(LogLevel::Error, "[ProcWorld] " + error);
    return false;
  }
  std::stringstream text;
  text << file.rdbuf();
  if (!Worldgen::Json::LoadRegionContent(text.str(), content, &error)) {
    Logger::getInstance().Log(LogLevel::Error,
                              "[ProcWorld] " + path.string() + ": " + error);
    return false;
  }
  return true;
}

static uint64_t _CellKey(const Worldgen::PlanetCell& cell) {
  return static_cast<uint64_t>(cell.face) << 60 |
         static_cast<uint64_t>(cell.level) << 52 |
         static_cast<uint64_t>(cell.i) << 26 | static_cast<uint64_t>(cell.j);
}

void Subsystem::Init() {}

void Subsystem::Shutdown() {
  meshes_.clear();
  planets_.clear();
}

MaterialHandle Subsystem::TerrainMaterial() {
  if (terrain_material_ == INVALID_MATERIAL) {
    Content::MaterialData material;
    material.name = "Region Terrain";
    material.is_pbr = true;
    material.base_color = glm::vec4(0.45f, 0.42f, 0.36f, 1.0f);
    material.roughness = 0.9f;
    terrain_material_ = MaterialRegistry::Instance().Register(material);
  }
  return terrain_material_;
}

void Subsystem::Update() {
  auto& ecs = ECS::Main();
  auto& resource_mgr = EngineContext::resourceManager();

  auto generator_of = [&](const VolumeComponent& volume) -> PCG::Generator* {
    auto resource =
        resource_mgr.GetResourceAs<PCG::GeneratorResource>(volume.generator_id);
    return resource ? resource->Get() : nullptr;
  };
  auto is_type = [](PCG::Generator* generator, PCG::GeneratorType type) {
    return generator && generator->GetType() == type;
  };

  // Volumes destroyed or given another generator since
  std::erase_if(meshes_, [&](const auto& entry) {
    return !ecs.Reg().valid(entry.first);
  });
  std::erase_if(planets_, [&](const auto& entry) {
    const Entity volume = entry.first;
    if (!ecs.Reg().valid(volume))
      return true;
    if (ecs.Has<VolumeComponent>(volume) &&
        is_type(generator_of(ecs.Get<VolumeComponent>(volume)),
                PCG::GeneratorType::Planet))
      return false;
    Clear(volume);
    return true;
  });

  for (auto [entity, volume] : ecs.View<VolumeComponent>().each()) {
    PCG::Generator* generator = generator_of(volume);
    if (is_type(generator, PCG::GeneratorType::Region) && volume.dirty) {
      Build(entity, static_cast<RegionGenerator&>(*generator));
      volume.dirty = false;
    } else if (is_type(generator, PCG::GeneratorType::Planet)) {
      Stream(entity, static_cast<PlanetGenerator&>(*generator),
             planets_[entity], volume.dirty);
      volume.dirty = false;
    }
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
  if (auto it = planets_.find(volume); it != planets_.end())
    it->second.regions.clear();
}

// TODO: build on a worker thread; the editor stalls while a region builds
// on the main thread
void Subsystem::Build(Entity volume, RegionGenerator& generator) {
  auto& ecs = ECS::Main();
  Clear(volume);
  generator.error.clear();
  generator.vertex_count = generator.instance_count = 0;

  Worldgen::RegionContent content;
  if (!_LoadContent(root_, generator.content_path, content, generator.error))
    return;

  glm::ivec3 cell = generator.cell;
  if (cell == glm::ivec3(0))
    cell = glm::ivec3(0, static_cast<int>(content.radius) + 16, 0);
  const int level = ecs.Get<VolumeComponent>(volume).lod_level;

  const auto start = Clock::now();
  const Worldgen::Region region =
      Worldgen::BuildRegion(content, cell.x, cell.y, cell.z, level);
  auto result = RegionInstancer::Instance(ecs, region, content, volume,
                                          TerrainMaterial(), root_);
  generator.build_ms = _Milliseconds(start);

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

// The planet's regions form a quadtree per cube face: a region splits while
// the viewer is near it, and the leaves are the regions loaded.
// TODO: keep a region until the regions replacing it are built (holes show
// for a few frames), and stitch the seams between levels
// TODO: floating origin; positions are planet-relative floats
void Subsystem::Stream(Entity volume, PlanetGenerator& generator,
                       PlanetState& state, bool reload) {
  auto& ecs = ECS::Main();
  if (reload) {
    Clear(volume);
    generator.error.clear();
    state.loaded = _LoadContent(root_, generator.content_path, state.content,
                                generator.error);
    state.bare_content = state.content;
    state.bare_content.objects.clear();
  }
  if (!state.loaded) {
    generator.loaded_regions = generator.queued_regions = 0;
    return;
  }

  const float radius = state.content.radius;
  const int finest = std::max(generator.finest_level, 0);
  const int coarsest = std::clamp(generator.coarsest_level, finest, 9);
  const glm::vec3 viewer = glm::inverse(ecs.Get<TransformComponent>(volume).model_) *
                           glm::vec4(viewer_, 1.0f);

  // The leaves around the viewer, with their distance
  struct Wanted {
    Worldgen::PlanetCell cell;
    float distance;
  };
  std::vector<Wanted> wanted;
  std::function<void(const Worldgen::PlanetCell&)> visit =
      [&](const Worldgen::PlanetCell& cell) {
        int centre[3];
        Worldgen::PlanetGrid::CellCentre(cell, radius, centre);
        const glm::vec3 surface =
            glm::normalize(glm::vec3(centre[0], centre[1], centre[2])) *
            radius;
        const float distance = glm::length(viewer - surface);
        const float width = static_cast<float>(32 << cell.level);
        if (cell.level <= finest ||
            distance >= generator.split_distance * width) {
          wanted.push_back({cell, distance});
          return;
        }
        const int cells =
            Worldgen::PlanetGrid::CellsPerSide(radius, cell.level - 1);
        for (int i = cell.i * 2; i < std::min(cell.i * 2 + 2, cells); i++)
          for (int j = cell.j * 2; j < std::min(cell.j * 2 + 2, cells); j++)
            visit({cell.face, i, j, cell.level - 1});
      };
  const int roots = Worldgen::PlanetGrid::CellsPerSide(radius, coarsest);
  for (int face = 0; face < 6; face++)
    for (int i = 0; i < roots; i++)
      for (int j = 0; j < roots; j++)
        visit({face, i, j, coarsest});

  // Unload the regions no longer wanted
  std::unordered_map<uint64_t, bool> keep;
  for (const Wanted& w : wanted)
    keep[_CellKey(w.cell)] = true;
  std::erase_if(state.regions, [&](auto& entry) {
    if (keep.count(entry.first))
      return false;
    ecs.DestroyEntity(entry.second.entity);
    return true;
  });

  // Build the nearest missing ones
  std::erase_if(wanted, [&](const Wanted& w) {
    return state.regions.count(_CellKey(w.cell)) > 0;
  });
  std::sort(wanted.begin(), wanted.end(),
            [](const Wanted& a, const Wanted& b) {
              return a.distance < b.distance;
            });
  const size_t builds = std::min(
      wanted.size(), static_cast<size_t>(std::max(generator.builds_per_frame, 1)));
  const auto start = Clock::now();
  for (size_t k = 0; k < builds; k++) {
    const Worldgen::PlanetCell& cell = wanted[k].cell;
    const Worldgen::RegionContent& content =
        cell.level <= generator.object_level ? state.content
                                             : state.bare_content;
    int centre[3];
    Worldgen::PlanetGrid::CellCentre(cell, radius, centre);
    const Worldgen::Region region = Worldgen::BuildRegion(
        content, centre[0], centre[1], centre[2], cell.level);

    const std::string name = "Region " + std::to_string(cell.face) + "/" +
                             std::to_string(cell.level) + "/" +
                             std::to_string(cell.i) + "/" +
                             std::to_string(cell.j);
    auto [entity, transform] = ecs.CreateEntity(name, volume);
    transform.position_ =
        glm::vec3(region.origin[0], region.origin[1], region.origin[2]);
    ecs.Add<ProceduralInstanceComponent>(entity, volume);
    auto result = RegionInstancer::Instance(ecs, region, content, entity,
                                            TerrainMaterial(), root_);
    state.regions[_CellKey(cell)] = {entity, std::move(result.terrain_mesh)};
  }
  generator.build_ms = builds ? _Milliseconds(start) : generator.build_ms;
  generator.loaded_regions = state.regions.size();
  generator.queued_regions = wanted.size() - builds;

  if (builds && generator.queued_regions == 0)
    Logger::getInstance().Log(
        LogLevel::Info, "[ProcWorld] " + generator.content_path + ": " +
                            std::to_string(generator.loaded_regions) +
                            " regions loaded");
}

}  // namespace ProcWorld
