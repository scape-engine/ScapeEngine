#include "procworld.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <sstream>
#include <thread>

#include "engine/context/engine_context.h"
#include "engine/pcg/pcg_engine.h"
#include "engine/core/logger.h"
#include "engine/ecs/components/procedural_instance_component.h"
#include "engine/ecs/components/volume_component.h"
#include "engine/pcg/generator_resource.h"
#include "engine/time/time.h"

#include "api/planet_grid.h"
#include "content_json.h"
#include "craft_generator.h"
#include "instancing/craft_instancer.h"
#include "instancing/region_instancer.h"
#include "planet_generator.h"
#include "region_generator.h"

namespace ProcWorld {

using Clock = std::chrono::steady_clock;

static float _Milliseconds(Clock::time_point start) {
  return std::chrono::duration<float, std::milli>(Clock::now() - start)
      .count();
}

static bool _Parse(const std::string& text, Worldgen::RegionContent& content,
                   std::string* error) {
  return Worldgen::Json::LoadRegionContent(text, content, error);
}

static bool _Parse(const std::string& text, Worldgen::PlanetContent& content,
                   std::string* error) {
  return Worldgen::Json::LoadPlanetContent(text, content, error);
}

static bool _Parse(const std::string& text, Worldgen::CraftContent& content,
                   std::string* error) {
  return Worldgen::Json::LoadCraftContent(text, content, error);
}

// Reads region, planet or craft content from a JSON file; false with `error` set on
// failure
template <class Content>
static bool _LoadContent(const std::filesystem::path& root,
                         const std::string& relative, Content& content,
                         std::string& error) {
  const std::filesystem::path path = root / relative;
  std::ifstream file(path);
  if (relative.empty() || !file) {
    error = "Can't open " + path.string();
    Logger::getInstance().Log(LogLevel::Error, "[ProcWorld] " + error);
    return false;
  }
  std::stringstream text;
  text << file.rdbuf();
  if (!_Parse(text.str(), content, &error)) {
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
  // The craft builds stop at their next craft
  stopping_ = true;
  crafts_.clear();
  discarded_craft_.clear();
  meshes_.clear();
  planets_.clear();  // waits for the builds still running
  discarded_.clear();
  for (auto& [volume, look] : looks_)
    Release(look);
  looks_.clear();
}

void Subsystem::Release(TerrainLook& look) {
  if (look.material != INVALID_MATERIAL)
    MaterialRegistry::Instance().Unregister(look.material);
  if (bgfx::isValid(look.tile_constants))
    bgfx::destroy(look.tile_constants);
  if (bgfx::isValid(look.images.texture))
    bgfx::destroy(look.images.texture);
  look = TerrainLook();
}

MaterialHandle Subsystem::Look(Entity volume, const TerrainMaterialData& data) {
  TerrainLook& look = looks_[volume];
  if (look.material != INVALID_MATERIAL && look.data == data)
    return look.material;

  TerrainLook previous = look;
  look.data = data;
  const bool reload = previous.material == INVALID_MATERIAL ||
                      previous.data.tile_images != data.tile_images;
  if (reload) {
    std::string error;
    look.images = TerrainMaterial::LoadTileImages(data, root_, error);
    if (!error.empty())
      Logger::getInstance().Log(LogLevel::Error, "[ProcWorld] " + error);
  }
  look.tile_constants = TerrainMaterial::CreateTileConstants(data, look.images);
  look.material = MaterialRegistry::Instance().Register(TerrainMaterial::Build(
      data, look.tile_constants, look.images.texture));
  if (previous.material == INVALID_MATERIAL)
    return look.material;
  if (!reload)
    previous.images = TerrainTileImages();  // still in use

  // Switch the volume's terrain over, then drop the old material
  auto& ecs = ECS::Main();
  std::function<void(Entity)> visit = [&](Entity entity) {
    if (ecs.Has<MeshRendererComponent>(entity)) {
      auto& renderer = ecs.Get<MeshRendererComponent>(entity);
      if (renderer.material_ == previous.material)
        renderer.material_ = look.material;
    }
    for (Entity child : ecs.Get<TransformComponent>(entity).children_)
      visit(child);
  };
  visit(volume);
  Release(previous);
  return look.material;
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
  std::erase_if(discarded_craft_, [](const auto& build) {
    return build.wait_for(std::chrono::seconds(0)) ==
           std::future_status::ready;
  });
  std::erase_if(crafts_, [&](auto& entry) {
    const Entity volume = entry.first;
    if (ecs.Reg().valid(volume) && ecs.Has<VolumeComponent>(volume) &&
        is_type(generator_of(ecs.Get<VolumeComponent>(volume)),
                PCG::GeneratorType::Craft))
      return false;
    if (ecs.Reg().valid(volume))
      Clear(volume);
    else if (entry.second.generating.valid())
      discarded_craft_.push_back(std::move(entry.second.generating));
    return true;
  });

  // Volumes destroyed or given another generator since
  std::erase_if(meshes_, [&](const auto& entry) {
    return !ecs.Reg().valid(entry.first);
  });
  std::erase_if(looks_, [&](auto& entry) {
    const Entity volume = entry.first;
    if (ecs.Reg().valid(volume) && ecs.Has<VolumeComponent>(volume)) {
      PCG::Generator* generator =
          generator_of(ecs.Get<VolumeComponent>(volume));
      if (is_type(generator, PCG::GeneratorType::Region) ||
          is_type(generator, PCG::GeneratorType::Planet))
        return false;
    }
    Release(entry.second);
    return true;
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

  const double now = Time::World();
  for (auto [entity, volume] : ecs.View<VolumeComponent>().each()) {
    PCG::Generator* generator = generator_of(volume);
    if (is_type(generator, PCG::GeneratorType::Region)) {
      auto& region = static_cast<RegionGenerator&>(*generator);
      Look(entity, region.look);
      if (volume.dirty)
        Build(entity, region);
      volume.dirty = false;
    } else if (is_type(generator, PCG::GeneratorType::Planet)) {
      auto& planet = static_cast<PlanetGenerator&>(*generator);
      PlanetState& state = planets_[entity];
      // A reload's colours apply from the next frame on, the look then
      // switching the terrain over
      TerrainMaterialData look = planet.look;
      for (int i = 0; i < TerrainMaterialData::tile_count; ++i)
        if (state.tile_colours[i].a > 0.0f)
          look.tile_colours[i] = glm::vec3(state.tile_colours[i]);
      Look(entity, look);
      Stream(entity, planet, state, volume.dirty);
      Weather(planet, state, now);
      Water(planet, state, now);
      volume.dirty = false;
    } else if (is_type(generator, PCG::GeneratorType::Craft)) {
      auto& craft = static_cast<CraftGenerator&>(*generator);
      if (volume.dirty)
        Build(entity, craft);
      volume.dirty = false;
      if (auto it = crafts_.find(entity); it != crafts_.end())
        Make(entity, craft, it->second);
    }
  }
}

// The events list is rebuilt only once its last event has ended, or when the
// clock is set back before it; the kind is picked once per event
void Subsystem::Weather(PlanetGenerator& generator, PlanetState& state,
                        double now) {
  generator.weather_events = !state.weather_events.never;
  generator.weather_event = generator.weather_event_warning = false;
  generator.weather_event_kind = -1;
  generator.weather_event_intensity = generator.weather_event_strength = 0.0f;
  generator.weather_event_change = -1.0;
  if (state.weather_events.never)
    return;
  const uint64_t time = static_cast<uint64_t>(now);
  if (state.events.empty() || state.events.back().ends <= time ||
      time < state.events.front().warning)
    state.events = Worldgen::WeatherEvents(
        state.weather_events, state.weather_seed, time, time);
  for (const Worldgen::WeatherEvent& e : state.events) {
    if (e.ends <= time)
      continue;
    if (time < e.warning) {
      generator.weather_event_change = static_cast<double>(e.warning) - now;
      return;
    }
    if (e.warning != state.picked_warning) {
      state.picked_warning = e.warning;
      Worldgen::PickWeatherEventKind(
          state.weather_event_kinds.kinds, state.weather_event_kinds.usable,
          e.warning, state.picked_kind, state.picked_intensity);
    }
    generator.weather_event = true;
    generator.weather_event_warning = time < e.starts;
    generator.weather_event_kind = state.picked_kind;
    generator.weather_event_intensity = state.picked_intensity;
    const double starts = static_cast<double>(e.starts),
                 full = static_cast<double>(e.full),
                 fades = static_cast<double>(e.fades),
                 ends = static_cast<double>(e.ends);
    double strength = 1.0;
    if (now < starts)
      strength = 0.0;
    else if (now < full)
      strength = (now - starts) / (full - starts);
    else if (now >= fades)
      strength = (ends - now) / (ends - fades);
    generator.weather_event_strength =
        static_cast<float>(std::clamp(strength, 0.0, 1.0));
    for (const uint64_t stage : {e.starts, e.full, e.fades, e.ends})
      if (time < stage) {
        generator.weather_event_change = static_cast<double>(stage) - now;
        break;
      }
    return;
  }
}

// A new state blends in over the rules' blend time; a reload starts without
// a blend. The water reads a weather event from its start to its end
void Subsystem::Water(PlanetGenerator& generator, const PlanetState& state,
                      double now) {
  if (!state.water_states) {
    generator.water_state = generator.water_state_from = -1;
    generator.water_blend = 1.0f;
    return;
  }
  const Worldgen::WaterStateRules& rules = *state.water_states;
  const bool event =
      generator.force_weather_event ||
      (generator.weather_event && !generator.weather_event_warning);
  const int pick =
      Worldgen::PickWaterState(rules, static_cast<uint64_t>(now), event);
  if (generator.water_state < 0) {
    generator.water_state = generator.water_state_from = pick;
    generator.water_blend = 1.0f;
  } else if (pick != generator.water_state) {
    generator.water_state_from = generator.water_state;
    generator.water_state = pick;
    generator.water_blend = 0.0f;
  }
  generator.water_blend =
      rules.blend_time > 0.0f
          ? std::min(1.0f, generator.water_blend +
                               Time::Deltaf() / rules.blend_time)
          : 1.0f;
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
  if (auto it = crafts_.find(volume); it != crafts_.end()) {
    if (it->second.generating.valid())
      discarded_craft_.push_back(std::move(it->second.generating));
    it->second = {};  // emptied, not erased: Update may be erasing it
  }
  if (auto it = planets_.find(volume); it != planets_.end()) {
    it->second.regions.clear();
    for (auto& [key, build] : it->second.building)
      discarded_.push_back(std::move(build));
    it->second.building.clear();
    it->second.herds.clear();  // destroyed above
    for (auto& [key, build] : it->second.herd_building)
      discarded_.push_back(std::move(build));
    it->second.herd_building.clear();
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

  // The planet's buildings, when it has any
  std::unique_ptr<BuildingLibrary> buildings;
  if (!generator.planet_path.empty()) {
    Worldgen::PlanetContent planet;
    if (!_LoadContent(root_, generator.planet_path, planet, generator.error))
      return;
    if (planet.buildings.active) {
      Worldgen::PlanetInput input;
      input.seed = generator.seed;
      content.buildings = Worldgen::BuildPlanet(planet, input).buildings;
      content.buildings.radius = content.radius;  // the terrain's
      buildings =
          std::make_unique<BuildingLibrary>(planet.building_styles, root_);
    }
  }

  glm::ivec3 cell = generator.cell;
  if (cell == glm::ivec3(0))
    cell = glm::ivec3(0, static_cast<int>(content.radius) + 16, 0);
  const int level = ecs.Get<VolumeComponent>(volume).lod_level;

  const auto start = Clock::now();
  const Worldgen::Region region =
      Worldgen::BuildRegion(content, cell.x, cell.y, cell.z, level);
  auto result = RegionInstancer::Instance(ecs, region, content, volume,
                                          looks_[volume].material, root_);
  const size_t creatures =
      RegionInstancer::InstanceHerds(ecs, region, content, volume, root_)
          .size();
  size_t built = 0;
  if (buildings) {
    const auto pieces =
        RegionInstancer::GatherBuildings(region, *buildings, root_);
    RegionInstancer::InstanceBuildings(ecs, pieces, volume, root_, nullptr);
    built = pieces.buildings;
  }
  generator.build_ms = _Milliseconds(start);

  generator.vertex_count = region.mesh.size();
  generator.instance_count =
      result.object_entities.size() + creatures + built;
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

void Subsystem::Build(Entity volume, CraftGenerator& generator) {
  auto& ecs = ECS::Main();
  Clear(volume);
  generator.error.clear();
  generator.craft_count = generator.missing_count = 0;
  generator.build_ms = 0.0f;

  Worldgen::CraftContent content;
  if (!_LoadContent(root_, generator.content_path, content, generator.error))
    return;

  const auto start = Clock::now();
  const VolumeComponent& bounds = ecs.Get<VolumeComponent>(volume);
  CraftState& state = crafts_[volume];
  state.plans = CraftInstancer::Plan(
      content, Worldgen::BuildFleet(content, generator.seed, generator.lawless),
      bounds.bounds_min, bounds.bounds_max, root_, generator.missing_count);
  // The procmodels not loaded yet are read here, and uploaded by Make; the
  // craft's textures composited here, and stored by Make
  state.generating = std::async(
      std::launch::async,
      [plans = state.plans, content = std::move(content),
       legacy = generator.legacy_colours, &stopping = stopping_,
       &procmodel = EngineContext::PCG().GetProcModel()]() mutable {
        CraftWork work;
        const auto start = Clock::now();
        for (CraftInstancer::CraftPlan& plan : plans) {
          if (stopping)
            return CraftWork();
          if (!plan.descriptor.empty() && !plan.archetype) {
            auto it = work.read.find(plan.descriptor);
            if (it == work.read.end())
              it = work.read
                       .emplace(plan.descriptor,
                                procmodel.ReadArchetype(plan.descriptor))
                       .first;
            plan.archetype = it->second;
          }
          work.instances.push_back(CraftInstancer::Generate(procmodel, plan));
        }
        // The textures on a few threads, leaving two cores to the rest
        ProcModel::Subsystem::TextureClaims claims;
        work.textures.resize(plans.size());
        std::atomic<size_t> next{0};
        const auto composite = [&] {
          for (size_t at; !stopping && (at = next++) < plans.size();)
            work.textures[at] = CraftInstancer::PrepareTextures(
                procmodel, content, plans[at], legacy, claims);
        };
        std::vector<std::future<void>> helpers;
        for (unsigned i = 3; i < std::thread::hardware_concurrency(); ++i)
          helpers.push_back(std::async(std::launch::async, composite));
        composite();
        for (auto& helper : helpers)
          helper.wait();
        Logger::getInstance().Log(
            LogLevel::Info, "[ProcWorld] " + std::to_string(plans.size()) +
                                " craft picked and textured, " +
                                std::to_string(_Milliseconds(start)) +
                                " ms on a worker");
        return work;
      });
  generator.build_ms = _Milliseconds(start);
}

void Subsystem::Make(Entity volume, CraftGenerator& generator,
                     CraftState& state) {
  if (state.generating.valid()) {
    if (state.generating.wait_for(std::chrono::seconds(0)) !=
        std::future_status::ready)
      return;
    state.work = state.generating.get();
    // Every craft's images before any craft takes them: a shared one is in
    // the set that claimed it
    for (auto& textures : state.work.textures)
      if (textures)
        EngineContext::PCG().GetProcModel().StoreTextureImages(*textures);
  }
  if (state.made == state.plans.size())
    return;

  auto& ecs = ECS::Main();
  const auto start = Clock::now();
  // The procmodels read, their meshes uploaded within the budget
  if (!state.work.read.empty()) {
    const auto read = state.work.read.begin();
    if (read->second) {
      read->second->GetGraph().UploadMeshes(frame_budget_ms);
      if (!read->second->GetGraph().Uploaded()) {
        generator.build_ms += _Milliseconds(start);
        return;
      }
    }
    const auto archetype = EngineContext::PCG().GetProcModel().AddArchetype(
        read->first, std::move(read->second));
    for (CraftInstancer::CraftPlan& plan : state.plans)
      if (plan.descriptor == read->first)
        plan.archetype = archetype;
    state.work.read.erase(read);
    generator.build_ms += _Milliseconds(start);
    return;
  }
  while (state.made < state.plans.size() &&
         _Milliseconds(start) < frame_budget_ms) {
    const size_t at = state.made++;
    const Entity entity = CraftInstancer::Make(
        ecs, state.plans[at], state.work.instances[at],
        std::move(state.work.textures[at]), volume, root_);
    if (entity == entt::null)
      generator.missing_count++;
    else
      generator.craft_count++;
  }
  generator.build_ms += _Milliseconds(start);
  if (state.made == state.plans.size())
    Logger::getInstance().Log(
        LogLevel::Info, "[ProcWorld] " + generator.content_path + ": " +
                            std::to_string(generator.craft_count) +
                            " craft, " +
                            std::to_string(generator.missing_count) +
                            " missing, " +
                            std::to_string(generator.build_ms) +
                            " ms on the main thread");
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

    // The planet itself, from its own content: its terrain settings and tile
    // colours replace the region content's and the look's
    state.tile_colours = {};
    state.textures = {};
    state.buildings = nullptr;
    state.water_states = nullptr;
    generator.water_state = generator.water_state_from = -1;
    state.weather_events = {.never = true};
    state.weather_event_kinds = {};
    state.weather_seed = generator.seed;
    state.events.clear();
    state.picked_warning = UINT64_MAX;
    generator.planet_tile_colours = 0;
    generator.planet_terrain = false;
    generator.planet_radius = 0.0f;
    Worldgen::PlanetContent planet;
    Worldgen::Planet built;
    if (!generator.planet_path.empty() &&
        _LoadContent(root_, generator.planet_path, planet, generator.error)) {
      Worldgen::PlanetInput input;
      input.seed = generator.seed;
      input.biome = generator.biome;
      input.subtype = generator.subtype;
      input.size = generator.size;
      built = Worldgen::BuildPlanet(planet, input);
      state.textures.seed = generator.seed;
      state.textures.palettes = built.palettes;
      generator.planet_terrain = !planet.terrain_types.empty();
      generator.planet_radius = built.radius;
      state.weather_events = built.weather_events;
      state.weather_event_kinds = built.weather_event_kinds;
      if (!planet.water_states.weights[0].empty() ||
          !planet.water_states.weights[1].empty() ||
          !planet.water_states.fallback[0].empty() ||
          !planet.water_states.fallback[1].empty())
        state.water_states = std::make_shared<Worldgen::WaterStateRules>(
            planet.water_states);
      for (int i = 0; i < TerrainMaterialData::tile_count; ++i) {
        const Worldgen::Colour& c = built.tile_colours[i];
        if (c.a > 0.0f) {
          state.tile_colours[i] = glm::vec4(c.r, c.g, c.b, 1.0f);
          ++generator.planet_tile_colours;
        }
      }
    }

    auto content = std::make_shared<Worldgen::RegionContent>();
    if (_LoadContent(root_, generator.content_path, *content,
                     generator.error)) {
      if (generator.planet_terrain)
        content->terrain = built.terrain;
      if (generator.planet_radius > 0.0f)
        content->radius = generator.planet_radius;
      if (planet.buildings.active) {
        content->buildings = built.buildings;
        content->buildings.radius = content->radius;  // the terrain's
        state.buildings = std::make_shared<BuildingLibrary>(
            planet.building_styles, root_);
      }
      auto bare = std::make_shared<Worldgen::RegionContent>(*content);
      bare->objects.clear();
      auto herds = std::make_shared<Worldgen::RegionContent>(*bare);
      // A planet with an ecosystem replaces the region content's herds with
      // its filled roles
      // TODO: air, water and cave roles; only ground herds walk the navmesh
      if (!planet.creatures.tables.empty()) {
        herds->herds.clear();
        herds->herd_sizes.clear();
        herds->herd_ids.clear();
        herds->herd_seeds.clear();
        for (const Worldgen::PlanetHerd& herd : built.herds) {
          if (herd.domain != 0)
            continue;
          const Worldgen::CreatureKind& kind =
              planet.creatures.creatures[herd.creature];
          herds->herds.push_back(herd.spawn);
          herds->herd_sizes.push_back(kind.size);
          herds->herd_ids.push_back(kind.id);
          herds->herd_seeds.push_back(herd.seed);
        }
      }
      state.herd_content = std::move(herds);
      content->herds.clear();
      bare->herds.clear();
      state.content = std::move(content);
      state.bare_content = std::move(bare);
    } else {
      state.content = state.bare_content = state.herd_content = nullptr;
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
        if (generator.view_distance > 0.0f &&
            distance - width > generator.view_distance)
          return;
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
  // TODO: visits every root each update (about 220k on a real-size planet);
  // walk only the roots near the viewer
  const int roots = Worldgen::PlanetGrid::CellsPerSide(radius, coarsest);
  for (int face = 0; face < 6; face++)
    for (int i = 0; i < roots; i++)
      for (int j = 0; j < roots; j++)
        visit({face, i, j, coarsest});
  std::unordered_map<uint64_t, bool> is_wanted;
  for (const Wanted& w : wanted)
    is_wanted[_CellKey(w.cell)] = true;

  // The herd-level cells over the wanted regions that fine or finer (coarser
  // ones are too far for creatures)
  const int herd_level = generator.herd_level;
  std::unordered_map<uint64_t, Worldgen::PlanetCell> herd_cells;
  if (herd_level >= 0 && state.herd_content &&
      !state.herd_content->herds.empty())
    for (const Wanted& w : wanted)
      if (w.cell.level <= herd_level) {
        const int shift = herd_level - w.cell.level;
        const Worldgen::PlanetCell cell{w.cell.face, w.cell.i >> shift,
                                        w.cell.j >> shift, herd_level};
        herd_cells.emplace(_CellKey(cell), cell);
      }
  for (auto it = state.herd_building.begin();
       it != state.herd_building.end();) {
    if (it->second.wait_for(std::chrono::seconds(0)) !=
        std::future_status::ready) {
      ++it;
      continue;
    }
    const Worldgen::Region region = it->second.get().region;
    if (herd_cells.count(it->first)) {
      const Worldgen::PlanetCell cell = _CellOf(it->first);
      auto [entity, transform] = ecs.CreateEntity(
          "Herds " + std::to_string(cell.face) + "/" +
              std::to_string(cell.level) + "/" + std::to_string(cell.i) + "/" +
              std::to_string(cell.j),
          volume);
      transform.position_ =
          glm::vec3(region.origin[0], region.origin[1], region.origin[2]);
      ecs.Add<ProceduralInstanceComponent>(entity, volume);
      ecs.Add<HerdComponent>(entity).settings_ = generator.creatures;
      RegionInstancer::InstanceHerds(ecs, region, *state.herd_content, entity,
                                     root_, &state.textures);
      state.herds[it->first] = entity;
    }
    it = state.herd_building.erase(it);
  }
  std::erase_if(state.herds, [&](auto& entry) {
    if (herd_cells.count(entry.first)) {
      if (auto* herd = ecs.Reg().try_get<HerdComponent>(entry.second))
        herd->settings_ = generator.creatures;
      return false;
    }
    ecs.DestroyEntity(entry.second);
    return true;
  });

  // Finished builds: instance the ones still wanted, within the frame budget
  const auto start = Clock::now();
  for (auto it = state.building.begin(); it != state.building.end();) {
    if (_Milliseconds(start) >= frame_budget_ms)
      break;
    if (it->second.wait_for(std::chrono::seconds(0)) !=
        std::future_status::ready) {
      ++it;
      continue;
    }
    const RegionBuild build = it->second.get();
    const Worldgen::Region& region = build.region;
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
                                    looks_[volume].material, root_,
                                    skirt_depth, &state.textures);
      RegionInstancer::InstanceBuildings(ecs, build.pieces, entity, root_,
                                         &state.textures);
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
    if (state.building.size() + state.herd_building.size() >= jobs)
      break;
    std::shared_ptr<const Worldgen::RegionContent> content =
        w.cell.level <= generator.object_level ? state.content
                                               : state.bare_content;
    int centre[3];
    Worldgen::PlanetGrid::CellCentre(w.cell, radius, centre);
    const int level = w.cell.level;
    // The region's buildings are made here too, off the main thread
    state.building[_CellKey(w.cell)] = std::async(
        std::launch::async, [content,
                             buildings = level <= generator.building_level
                                             ? state.buildings
                                             : nullptr,
                             x = centre[0], y = centre[1], z = centre[2],
                             level, root = root_] {
          RegionBuild build{Worldgen::BuildRegion(*content, x, y, z, level)};
          if (buildings)
            build.pieces = RegionInstancer::GatherBuildings(build.region,
                                                            *buildings, root);
          return build;
        });
    started++;
  }
  // Herds after the ground
  // TODO: a herd cell that is also a wanted region is built twice
  for (const auto& [key, cell] : herd_cells) {
    if (state.building.size() + state.herd_building.size() >= jobs)
      break;
    if (state.herds.count(key) || state.herd_building.count(key))
      continue;
    int centre[3];
    Worldgen::PlanetGrid::CellCentre(cell, radius, centre);
    state.herd_building[key] = std::async(
        std::launch::async, [content = state.herd_content, x = centre[0],
                             y = centre[1], z = centre[2], level = cell.level] {
          return RegionBuild{Worldgen::BuildRegion(*content, x, y, z, level)};
        });
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
