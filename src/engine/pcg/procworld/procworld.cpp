#include "procworld.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <sstream>
#include <thread>

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
  planets_.clear();  // waits for the builds still running
  discarded_.clear();
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

  std::erase_if(discarded_, [](const auto& build) {
    return build.wait_for(std::chrono::seconds(0)) ==
           std::future_status::ready;
  });

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
  if (auto it = planets_.find(volume); it != planets_.end()) {
    it->second.regions.clear();
    for (auto& [key, build] : it->second.building)
      discarded_.push_back(std::move(build));
    it->second.building.clear();
  }
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

// Whether two cells of one face overlap (either holds the other)
static bool _Overlap(const Worldgen::PlanetCell& a,
                     const Worldgen::PlanetCell& b) {
  if (a.face != b.face)
    return false;
  const int level = std::max(a.level, b.level);
  return a.i >> (level - a.level) == b.i >> (level - b.level) &&
         a.j >> (level - a.level) == b.j >> (level - b.level);
}

static Worldgen::PlanetCell _CellOf(uint64_t key) {
  return {static_cast<int>(key >> 60),
          static_cast<int>(key >> 26 & 0x3ffffff),
          static_cast<int>(key & 0x3ffffff),
          static_cast<int>(key >> 52 & 0xff)};
}

// The planet's regions form a quadtree per cube face: a region splits while
// the viewer is near it, and the leaves are the regions wanted. They build on
// worker threads, nearest first; a region stays until the regions replacing
// it are loaded, so no holes show.
// TODO: floating origin; positions are planet-relative floats
void Subsystem::Stream(Entity volume, PlanetGenerator& generator,
                       PlanetState& state, bool reload) {
  auto& ecs = ECS::Main();
  if (reload) {
    Clear(volume);
    generator.error.clear();
    auto content = std::make_shared<Worldgen::RegionContent>();
    if (_LoadContent(root_, generator.content_path, *content,
                     generator.error)) {
      auto bare = std::make_shared<Worldgen::RegionContent>(*content);
      bare->objects.clear();
      state.content = std::move(content);
      state.bare_content = std::move(bare);
    } else {
      state.content = state.bare_content = nullptr;
    }
  }
  if (!state.content) {
    generator.loaded_regions = generator.queued_regions =
        generator.building_regions = 0;
    return;
  }

  const float radius = state.content->radius;
  const int finest = std::max(generator.finest_level, 0);
  const int coarsest = std::clamp(generator.coarsest_level, finest, 9);
  const glm::vec3 viewer =
      glm::inverse(ecs.Get<TransformComponent>(volume).model_) *
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
  std::unordered_map<uint64_t, bool> is_wanted;
  for (const Wanted& w : wanted)
    is_wanted[_CellKey(w.cell)] = true;

  // Finished builds: instance the ones still wanted
  for (auto it = state.building.begin(); it != state.building.end();) {
    if (it->second.wait_for(std::chrono::seconds(0)) !=
        std::future_status::ready) {
      ++it;
      continue;
    }
    const Worldgen::Region region = it->second.get();
    const Worldgen::PlanetCell cell = _CellOf(it->first);
    if (is_wanted.count(it->first)) {
      const Worldgen::RegionContent& content =
          cell.level <= generator.object_level ? *state.content
                                               : *state.bare_content;
      const std::string name =
          "Region " + std::to_string(cell.face) + "/" +
          std::to_string(cell.level) + "/" + std::to_string(cell.i) + "/" +
          std::to_string(cell.j);
      auto [entity, transform] = ecs.CreateEntity(name, volume);
      transform.position_ =
          glm::vec3(region.origin[0], region.origin[1], region.origin[2]);
      ecs.Add<ProceduralInstanceComponent>(entity, volume);
      // Deep enough to cover the height differences between levels
      const float skirt_depth = static_cast<float>(32 << cell.level) * 0.25f;
      auto result =
          RegionInstancer::Instance(ecs, region, content, entity,
                                    TerrainMaterial(), root_, skirt_depth);
      state.regions[it->first] = {entity, std::move(result.terrain_mesh)};
    }
    it = state.building.erase(it);
  }

  // Unload the regions no longer wanted once every wanted region over their
  // ground is loaded
  std::erase_if(state.regions, [&](auto& entry) {
    if (is_wanted.count(entry.first))
      return false;
    const Worldgen::PlanetCell cell = _CellOf(entry.first);
    for (const Wanted& w : wanted) {
      if (_Overlap(cell, w.cell) && !state.regions.count(_CellKey(w.cell)))
        return false;
    }
    ecs.DestroyEntity(entry.second.entity);
    return true;
  });

  // Start the nearest missing ones
  std::erase_if(wanted, [&](const Wanted& w) {
    const uint64_t key = _CellKey(w.cell);
    return state.regions.count(key) > 0 || state.building.count(key) > 0;
  });
  std::sort(wanted.begin(), wanted.end(),
            [](const Wanted& a, const Wanted& b) {
              return a.distance < b.distance;
            });
  const int cores = static_cast<int>(std::thread::hardware_concurrency());
  const size_t jobs = static_cast<size_t>(
      generator.worker_jobs > 0 ? generator.worker_jobs
                                : std::max(1, cores - 1));
  size_t started = 0;
  for (const Wanted& w : wanted) {
    if (state.building.size() >= jobs)
      break;
    std::shared_ptr<const Worldgen::RegionContent> content =
        w.cell.level <= generator.object_level ? state.content
                                               : state.bare_content;
    int centre[3];
    Worldgen::PlanetGrid::CellCentre(w.cell, radius, centre);
    const int level = w.cell.level;
    state.building[_CellKey(w.cell)] = std::async(
        std::launch::async, [content, x = centre[0], y = centre[1],
                             z = centre[2], level] {
          return Worldgen::BuildRegion(*content, x, y, z, level);
        });
    started++;
  }

  const bool was_loading =
      generator.queued_regions + generator.building_regions > 0;
  generator.loaded_regions = state.regions.size();
  generator.building_regions = state.building.size();
  generator.queued_regions = wanted.size() - started;
  if (was_loading &&
      generator.queued_regions + generator.building_regions == 0)
    Logger::getInstance().Log(
        LogLevel::Info, "[ProcWorld] " + generator.content_path + ": " +
                            std::to_string(generator.loaded_regions) +
                            " regions loaded");
}

}  // namespace ProcWorld
