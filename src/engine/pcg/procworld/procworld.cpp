#include "procworld.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>

#include "engine/diagnostics/frame_stats.h"
#include "engine/context/engine_context.h"
#include "engine/pcg/pcg_engine.h"
#include "engine/core/logger.h"
#include "engine/ecs/components/fade_component.h"
#include "engine/ecs/components/procedural_instance_component.h"
#include "engine/ecs/components/volume_component.h"
#include "engine/ecs/components/wave_field_component.h"
#include "engine/paging/cell_jobs.h"
#include "engine/paging/frame_budget.h"
#include "engine/paging/jobs.h"
#include "engine/paging/viewers.h"
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

static bool _Parse(const std::string& text, Worldgen::RegionObject& object,
                   std::string* error) {
  return Worldgen::Json::LoadRegionObject(text, object, error);
}

static bool _Parse(const std::string& text, Worldgen::CraftContent& content,
                   std::string* error) {
  return Worldgen::Json::LoadCraftContent(text, content, error);
}

// Reads region, planet, object or craft content from a JSON file; false with `error` set on
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

// A cell's edits on the objects its rules placed (PCG::ApplyEdits), with
// their seeds and keys. Objects are known by their content id; an edited one
// becomes the kind of `content` its edit names, by `edit_objects` (an
// .object asset) else by id, and is dropped when there is none. Returns how
// many edits found no object
// TODO: edits on the buildings and herds stages, and on a region generator's
// objects
static size_t _EditObjects(
    const PCG::CellEdits& edits, const Worldgen::RegionContent& content,
    const std::unordered_map<std::string, int>& edit_objects,
    Worldgen::Region& region, RegionInstancer::ObjectTags& tags) {
  tags = RegionInstancer::Tags(region);
  std::vector<PCG::InstancePoint> points;
  for (size_t kind = 0; kind < region.objects.size(); ++kind) {
    const Worldgen::ObjectInstances& instances = region.objects[kind];
    for (size_t i = 0; i < instances.matrices.size(); ++i)
      points.push_back({content.objects[instances.object].id,
                        instances.matrices[i], tags[kind][i].seed,
                        instances.object});
  }
  const size_t orphans = PCG::ApplyEdits(edits, points).size();

  region.objects.clear();
  tags.clear();
  std::unordered_map<int, size_t> slots;  // by kind
  for (const PCG::InstancePoint& point : points) {
    int kind = point.kind;
    if (kind < 0) {
      if (const auto found = edit_objects.find(point.object);
          found != edit_objects.end())
        kind = found->second;
      else
        for (size_t k = 0; k < content.objects.size() && kind < 0; ++k)
          if (content.objects[k].id == point.object)
            kind = static_cast<int>(k);
      if (kind < 0)
        continue;
    }
    auto [slot, added] = slots.try_emplace(kind, region.objects.size());
    if (added) {
      region.objects.push_back({kind, {}});
      tags.emplace_back();
    }
    region.objects[slot->second].matrices.push_back(point.matrix);
    tags[slot->second].push_back({point.seed, point.key});
  }
  return orphans;
}

// Whether a cell built with `built` edits has the layer's
static bool _SameEdits(const std::optional<PCG::CellEdits>& built,
                       const PCG::EditLayer* layer, uint64_t key) {
  const PCG::CellEdits* now = layer ? layer->Find("objects", key) : nullptr;
  if (!built || built->edits.empty() && built->additions.empty())
    return !now;
  return now && *now == *built;
}

std::optional<Subsystem::CellPoint> Subsystem::Placement(
    Entity volume, const std::string& object, const glm::vec3& point) const {
  const auto planet = planets_.find(volume);
  if (planet == planets_.end() || !planet->second.content)
    return std::nullopt;
  const Worldgen::RegionContent& content = *planet->second.content;
  Worldgen::RegionObject kind;
  std::string error;
  if (!_LoadContent(root_, object, kind, error))
    return std::nullopt;
  int level = 0;
  while (level <= 9 && kind.placement_size != 32 << level)
    ++level;
  if (level > 9)
    return std::nullopt;

  const glm::vec3 at =
      glm::inverse(ECS::Main().Get<TransformComponent>(volume).model_) *
      glm::vec4(point, 1.0f);
  if (glm::dot(at, at) == 0.0f)
    return std::nullopt;
  const float p[3] = {at.x, at.y, at.z};
  const Worldgen::PlanetCell cell =
      Worldgen::PlanetGrid::CellAt(p, content.radius, level);
  // The cell's region origin, as BuildRegion finds it, from the terrain's
  // noise rather than the detail map it builds
  int centre[3];
  Worldgen::PlanetGrid::CellCentre(cell, content.radius, centre);
  const int k = std::clamp(level, 0, 5);
  const Worldgen::VoxelRegionInput region =
      Worldgen::VoxelGenerator::RegionInit(centre[0], centre[1], centre[2],
                                           level, content.radius,
                                           content.columns[k],
                                           content.layers[k]);
  const float zero[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float origin[4] = {};
  Worldgen::VoxelGenerator::LocalToSurface(region, content.terrain, nullptr, 0,
                                           zero, origin);

  // Up from the planet's centre, facing along any line across it
  const glm::vec3 up = glm::normalize(at);
  const glm::vec3 across = std::abs(up.y) < 0.99f
                               ? glm::vec3(0.0f, 1.0f, 0.0f)
                               : glm::vec3(1.0f, 0.0f, 0.0f);
  const glm::vec3 x = glm::normalize(glm::cross(up, across));
  const glm::vec3 z = glm::cross(x, up);
  const glm::vec3 t = at - glm::vec3(origin[0], origin[1], origin[2]);
  return CellPoint{"objects", _CellKey(cell),
                   {x.x, x.y, x.z, 0.0f, up.x, up.y, up.z, 0.0f, z.x, z.y, z.z,
                    0.0f, t.x, t.y, t.z, 1.0f}};
}

void Subsystem::Init() {}

// After Jobs::Shutdown: no job still reads what this frees
void Subsystem::Shutdown() {
  crafts_.clear();
  meshes_.clear();
  planets_.clear();
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

  std::erase_if(crafts_, [&](auto& entry) {
    const Entity volume = entry.first;
    if (ecs.Reg().valid(volume) && ecs.Has<VolumeComponent>(volume) &&
        is_type(generator_of(ecs.Get<VolumeComponent>(volume)),
                PCG::GeneratorType::Craft))
      return false;
    if (ecs.Reg().valid(volume))
      Clear(volume);
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
      Diagnostics::ScopedSection section("Planets");
      auto& planet = static_cast<PlanetGenerator&>(*generator);
      PlanetState& state = planets_[entity];
      // A reload's colours apply from the next frame on, the look then
      // switching the terrain over
      TerrainMaterialData look = planet.look;
      for (int i = 0; i < TerrainMaterialData::tile_count; ++i)
        if (state.tile_colours[i].a > 0.0f)
          look.tile_colours[i] = glm::vec3(state.tile_colours[i]);
      Look(entity, look);
      Stream(entity, planet, state, volume.dirty,
             resource_mgr
                 .GetResourceAs<PCG::GeneratorResource>(volume.generator_id)
                 ->edit_layer_);
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

  auto& ecs = ECS::Main();
  if (!ecs.Reg().valid(state.sea) || !ecs.Has<WaveFieldComponent>(state.sea))
    return;
  Worldgen::WaveField& field = ecs.Get<WaveFieldComponent>(state.sea).field;
  // The first state shows at once
  if (field.state != generator.water_state)
    Worldgen::SetWaterState(field, generator.water_state,
                            field.state < 0 ? 0.0f : rules.blend_time);
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
  // Builds still running finish unread
  if (auto it = crafts_.find(volume); it != crafts_.end())
    it->second = {};  // emptied, not erased: Update may be erasing it
  if (auto it = planets_.find(volume); it != planets_.end()) {
    it->second.regions.clear();
    it->second.leaving.clear();  // destroyed above
    it->second.building.Clear();
    it->second.herds.clear();  // destroyed above
    it->second.herd_building.Clear();
    it->second.placements.clear();  // destroyed above
    it->second.old_placements.clear();
    it->second.placing.Clear();
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
      result.objects + creatures + built;
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
  state.generating = Jobs::Submit(
      [plans = state.plans, content = std::move(content),
       legacy = generator.legacy_colours,
       &procmodel = EngineContext::PCG().GetProcModel()]() mutable {
        CraftWork work;
        const auto start = Clock::now();
        for (CraftInstancer::CraftPlan& plan : plans) {
          if (Jobs::Stopping())
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
        // The textures on the idle workers too
        ProcModel::Subsystem::TextureClaims claims;
        work.textures.resize(plans.size());
        Jobs::ForEach(plans.size(), [&](size_t at) {
          if (!Jobs::Stopping())
            work.textures[at] = CraftInstancer::PrepareTextures(
                procmodel, content, plans[at], legacy, claims);
        });
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

  if (!FrameBudget::More())
    return;
  auto& ecs = ECS::Main();
  const FrameBudget::Spend spend;
  const auto start = Clock::now();
  // The procmodels read, their meshes uploaded within the budget
  if (!state.work.read.empty()) {
    const auto read = state.work.read.begin();
    if (read->second) {
      read->second->GetGraph().UploadMeshes(FrameBudget::Left());
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
  while (state.made < state.plans.size() && FrameBudget::More()) {
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

// Makes the entities fade in over `seconds` (0: they show at once)
static void _FadeIn(ECS& ecs, const std::vector<Entity>& entities,
                    float seconds) {
  if (seconds <= 0.0f)
    return;
  for (const Entity entity : entities)
    ecs.Reg().emplace_or_replace<FadeComponent>(entity, 0.0f, 1.0f, seconds,
                                                false);
}

// Fades the entity out from where it is over `seconds`, then destroys it
// (0: at once)
static void _FadeOut(ECS& ecs, Entity entity, float seconds) {
  if (!ecs.Reg().valid(entity))
    return;
  if (seconds <= 0.0f) {
    ecs.DestroyEntity(entity);
    return;
  }
  const auto* fade = ecs.Reg().try_get<FadeComponent>(entity);
  ecs.Reg().emplace_or_replace<FadeComponent>(
      entity, fade ? fade->visibility_ : 1.0f, 0.0f, seconds, true);
}

// Fades a stage's entity in or out as its stage is on or off; one already
// fading out to go is left to it
static void _ShowStage(ECS& ecs, Entity entity, bool shown, float seconds) {
  if (!ecs.Reg().valid(entity))
    return;
  const auto* fade = ecs.Reg().try_get<FadeComponent>(entity);
  if (fade && fade->destroy_when_hidden_)
    return;
  const float target = shown ? 1.0f : 0.0f;
  if (fade ? fade->target_ == target : shown)
    return;
  ecs.Reg().emplace_or_replace<FadeComponent>(
      entity, fade ? fade->visibility_ : 1.0f, target, seconds, false);
}

// Whether the entity has finished fading in
static bool _Shown(ECS& ecs, Entity entity) {
  const auto* fade = ecs.Reg().try_get<FadeComponent>(entity);
  return !fade || fade->visibility_ >= 1.0f;
}

// The planet's regions form a quadtree per cube face: a region splits while
// the viewer is near it, and the leaves are the regions wanted. They build on
// worker threads, nearest first; a region stays until the regions replacing
// it are loaded, so no holes show.
// TODO: floating origin; positions are planet-relative floats
void Subsystem::Stream(Entity volume, PlanetGenerator& generator,
                       PlanetState& state, bool reload,
                       std::shared_ptr<const PCG::EditLayer> edits) {
  auto& ecs = ECS::Main();
  // A changed edit layer remakes the object cells whose edits changed
  // (below); one naming an object not loaded yet reloads
  std::shared_ptr<const PCG::EditLayer> previous_edits;
  bool edits_changed = false;
  if (!reload && edits != state.edits) {
    if (edits && state.edit_objects)
      for (const std::string& object : edits->Objects())
        reload |= !state.edit_objects->count(object);
    if (!reload) {
      previous_edits = std::exchange(state.edits, edits);
      edits_changed = true;
    }
  }
  if (reload) {
    Clear(volume);
    generator.error.clear();
    state.edits = std::move(edits);

    // The planet itself, from its own content: its terrain settings and tile
    // colours replace the region content's and the look's
    state.tile_colours = {};
    state.archetypes.clear();  // reloaded with the content
    state.textures = {};
    state.buildings = nullptr;
    state.water_states = nullptr;
    state.water_waves = nullptr;
    state.sea = entt::null;  // destroyed by Clear
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
      if (content->terrain.sea_level > 0.0f &&
          !planet.water_waves.states.empty()) {
        state.water_waves =
            std::make_shared<Worldgen::WaterWaveSettings>(planet.water_waves);
        auto [sea, transform] = ecs.CreateEntity("Sea", volume);
        ecs.Add<ProceduralInstanceComponent>(sea, volume, "sea");
        EngineContext::PCG().GetWater().Make(
            ecs, sea, state.water_waves,
            content->radius + content->terrain.sea_level,
            generator.sea.water);
        state.sea = sea;
      }
      if (planet.buildings.active) {
        content->buildings = built.buildings;
        content->buildings.radius = content->radius;  // the terrain's
        state.buildings = std::make_shared<BuildingLibrary>(
            planet.building_styles, root_);
      }
      // The planet's object kinds; one that doesn't load is left out (and
      // logged)
      for (const std::string& path : generator.objects.assets) {
        Worldgen::RegionObject object;
        std::string error;
        if (_LoadContent(root_, path, object, error))
          content->objects.push_back(std::move(object));
      }
      auto herds = std::make_shared<Worldgen::RegionContent>(*content);
      herds->objects.clear();
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
      // The objects' content keeps the buildings, which objects keep clear of
      auto objects = std::make_shared<Worldgen::RegionContent>(*content);
      content->objects.clear();
      // Their procmodels load here, on the main thread, so the workers can
      // generate the instances
      static constexpr std::string_view prefix = "procmodel:";
      const auto load = [&](const std::string& id) {
        if (id.rfind(prefix, 0) != 0 || state.archetypes.count(id))
          return;
        state.archetypes[id] =
            EngineContext::PCG().GetProcModel().Archetype(
                (root_ / id.substr(prefix.size())).string());
      };
      // The objects the edits name, after the planet's own: no rule places
      // them. One that doesn't load is left out (and logged)
      auto instancing = std::shared_ptr<Worldgen::RegionContent>(objects);
      auto edit_objects = std::make_shared<std::unordered_map<std::string, int>>();
      if (state.edits)
        for (const std::string& path : state.edits->Objects()) {
          Worldgen::RegionObject object;
          std::string error;
          if (!_LoadContent(root_, path, object, error))
            continue;
          if (instancing == objects)
            instancing = std::make_shared<Worldgen::RegionContent>(*objects);
          (*edit_objects)[path] = static_cast<int>(instancing->objects.size());
          instancing->objects.push_back(std::move(object));
        }
      for (const auto& object : instancing->objects)
        load(object.id);
      // The levels of the objects, the edits' too
      state.object_levels.clear();
      for (const Worldgen::RegionObject& object : instancing->objects)
        for (int level = 0; level <= 9; ++level) {
          if (object.placement_size != 32 << level)
            continue;
          auto found = std::find_if(
              state.object_levels.begin(), state.object_levels.end(),
              [&](const auto& objects) { return objects.level == level; });
          if (found == state.object_levels.end())
            found = state.object_levels.insert(found, {level, 0.0f});
          const bool limited = object.spawn.fade_end > object.spawn.fade_start;
          found->reach = std::max(
              found->reach, limited ? object.spawn.fade_end
                                    : std::numeric_limits<float>::infinity());
        }
      for (const std::string& id : state.herd_content->herd_ids)
        load(id);
      state.content = std::move(content);
      state.object_content = std::move(objects);
      state.instance_content = std::move(instancing);
      state.edit_objects = std::move(edit_objects);
      state.draw_version = UINT64_MAX;  // imposters looked up below
    } else {
      state.content = state.herd_content = state.object_content =
          state.instance_content = nullptr;
      state.edit_objects = nullptr;
    }
  }
  if (!state.content) {
    generator.loaded_regions = generator.queued_regions =
        generator.building_regions = 0;
    return;
  }

  // The object kinds' imposters bake over the frames before the cells need
  // them. Draw-settings edits remake them: the cells are made again, each old
  // one going as its new one is made
  auto& procmodels = EngineContext::PCG().GetProcModel();
  if (state.draw_version != procmodels.DrawVersion()) {
    if (state.draw_version != UINT64_MAX)
      for (auto& [key, cell] : state.placements) {
        if (auto old = state.old_placements.find(key);
            old != state.old_placements.end())
          _FadeOut(ecs, old->second.entity, generator.fade_time);
        state.old_placements[key] = cell;
      }
    state.placements.clear();
    state.draw_version = procmodels.DrawVersion();
    state.object_archetypes.clear();
    static constexpr std::string_view prefix = "procmodel:";
    if (state.instance_content)
      for (const auto& object : state.instance_content->objects) {
        const auto archetype = state.archetypes.find(object.id);
        if (archetype == state.archetypes.end() ||
            procmodels.Imposter(
                (root_ / object.id.substr(prefix.size())).string(),
                &state.textures))
          continue;
        state.object_archetypes.insert(*archetype);
      }
  }
  // Cells whose edits changed are made again, each old one kept until its
  // new one is made
  if (edits_changed)
    for (auto it = state.placements.begin(); it != state.placements.end();) {
      const PCG::CellEdits* before =
          previous_edits ? previous_edits->Find("objects", it->first) : nullptr;
      const PCG::CellEdits* after =
          state.edits ? state.edits->Find("objects", it->first) : nullptr;
      if (before == after || (before && after && *before == *after)) {
        ++it;
        continue;
      }
      if (auto old = state.old_placements.find(it->first);
          old != state.old_placements.end())
        _FadeOut(ecs, old->second.entity, generator.fade_time);
      state.old_placements[it->first] = it->second;
      it = state.placements.erase(it);
    }
  // Fades out the cell's old one, if any
  const auto retire = [&](uint64_t key) {
    if (const auto old = state.old_placements.find(key);
        old != state.old_placements.end()) {
      _FadeOut(ecs, old->second.entity, generator.fade_time);
      state.old_placements.erase(old);
    }
  };

  const float radius = state.content->radius;
  const TerrainStage& terrain = generator.terrain;
  const int finest = std::max(terrain.grid, 0);
  const int coarsest = std::clamp(terrain.coarsest_level, finest, 9);
  // The viewers in the planet's space; it loads around each of them
  const glm::mat4 to_planet =
      glm::inverse(ecs.Get<TransformComponent>(volume).model_);
  std::vector<glm::vec3> viewers;
  for (const glm::vec3& position : Viewers::Positions())
    viewers.push_back(to_planet * glm::vec4(position, 1.0f));
  // From the nearest viewer (infinite: none)
  const auto viewer_distance = [&](const glm::vec3& point) {
    float nearest = std::numeric_limits<float>::infinity();
    for (const glm::vec3& viewer : viewers)
      nearest = std::min(nearest, glm::distance(viewer, point));
    return nearest;
  };

  // From the nearest viewer to a cell's centre on the surface
  const auto distance_to = [&](const Worldgen::PlanetCell& cell) {
    int centre[3];
    Worldgen::PlanetGrid::CellCentre(cell, radius, centre);
    return viewer_distance(
        glm::normalize(glm::vec3(centre[0], centre[1], centre[2])) * radius);
  };

  // The leaves around the viewers, with their distance
  struct Wanted {
    Worldgen::PlanetCell cell;
    float distance;
  };
  std::vector<Wanted> wanted;
  std::function<void(const Worldgen::PlanetCell&)> visit =
      [&](const Worldgen::PlanetCell& cell) {
        const float distance = distance_to(cell);
        const float width = static_cast<float>(32 << cell.level);
        if (terrain.generation_radius > 0.0f &&
            distance - width > terrain.generation_radius)
          return;
        if (cell.level <= finest ||
            distance >= terrain.split_distance * width) {
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
  // ones are too far for creatures), within the stage's radius
  const HerdStage& herds = generator.herds;
  const int herd_level = herds.grid;
  std::unordered_map<uint64_t, Worldgen::PlanetCell> herd_cells;
  if (herds.enabled && herd_level >= 0 && state.herd_content &&
      !state.herd_content->herds.empty())
    for (const Wanted& w : wanted)
      if (w.cell.level <= herd_level &&
          (herds.generation_radius <= 0.0f ||
           w.distance <= herds.generation_radius)) {
        const int shift = herd_level - w.cell.level;
        const Worldgen::PlanetCell cell{w.cell.face, w.cell.i >> shift,
                                        w.cell.j >> shift, herd_level};
        herd_cells.emplace(_CellKey(cell), cell);
      }
  // Off, it keeps (hidden) those it has
  if (!herds.enabled)
    for (const auto& entry : state.herds)
      herd_cells.emplace(entry.first, _CellOf(entry.first));
  state.herd_building.Finish([&](uint64_t key, RegionBuild build) {
    const Worldgen::Region& region = build.region;
    if (herd_cells.count(key)) {
      const Worldgen::PlanetCell cell = _CellOf(key);
      auto [entity, transform] = ecs.CreateEntity(
          "Herds " + std::to_string(cell.face) + "/" +
              std::to_string(cell.level) + "/" + std::to_string(cell.i) + "/" +
              std::to_string(cell.j),
          volume);
      transform.position_ =
          glm::vec3(region.origin[0], region.origin[1], region.origin[2]);
      ecs.Add<ProceduralInstanceComponent>(entity, volume, "herds");
      ecs.Add<HerdComponent>(entity).settings_ = herds.creatures;
      RegionInstancer::InstanceHerds(ecs, region, *state.herd_content, entity,
                                     root_, &state.textures, &build.objects);
      _FadeIn(ecs, {entity}, generator.fade_time);
      state.herds[key] = entity;
    }
  });
  std::erase_if(state.herds, [&](auto& entry) {
    if (herd_cells.count(entry.first)) {
      if (auto* herd = ecs.Reg().try_get<HerdComponent>(entry.second))
        herd->settings_ = herds.creatures;
      return false;
    }
    _FadeOut(ecs, entry.second, generator.fade_time);
    return true;
  });

  // The cells of each object level within its kinds' reach, whatever the
  // terrain's detail there (their regions are built apart): their objects
  // fade out by distance before the cells go. A level without a reach places
  // as far as the terrain is that fine
  struct Placement {
    Worldgen::PlanetCell cell;
    float distance;
  };
  std::unordered_map<uint64_t, Placement> placement_cells;
  const auto place = [&](const Worldgen::PlanetCell& cell, float distance) {
    auto [it, added] =
        placement_cells.try_emplace(_CellKey(cell), Placement{cell, distance});
    it->second.distance = std::min(it->second.distance, distance);
  };
  // The farthest an object of the level draws (infinite: no limit), within
  // the stage's radius
  const auto reach_of = [&](int level) {
    for (const auto& objects : state.object_levels)
      if (objects.level == level)
        return generator.objects.generation_radius > 0.0f
                   ? std::min(objects.reach, generator.objects.generation_radius)
                   : objects.reach;
    return 0.0f;
  };
  // A cell's objects lie within this share of its width from its centre
  constexpr float extent = 0.75f;
  // Cells load this many of their widths before their objects' reach, so
  // they're made before the objects can show
  constexpr float lead = 1.0f;
  // Off, it keeps (hidden) those it has
  if (!generator.objects.enabled)
    for (const auto& entry : state.placements) {
      const Worldgen::PlanetCell cell = _CellOf(entry.first);
      place(cell, distance_to(cell));
    }
  else if (state.object_content)
    for (const auto& objects : state.object_levels) {
      const int level = objects.level;
      const bool limited = std::isfinite(reach_of(level));
      const float cell_width = static_cast<float>(32 << level);
      const float reach = reach_of(level) + lead * cell_width;
      for (const Wanted& w : wanted) {
        const float width = static_cast<float>(32 << w.cell.level);
        if (limited && w.distance - extent * width > reach)
          continue;
        if (w.cell.level <= level) {
          const int shift = level - w.cell.level;
          place({w.cell.face, w.cell.i >> shift, w.cell.j >> shift, level},
                w.distance);
          continue;
        }
        if (!limited)
          continue;
        // A coarser leaf: its cells of the level within reach
        const int shift = w.cell.level - level;
        const int cells = Worldgen::PlanetGrid::CellsPerSide(radius, level);
        for (int i = w.cell.i << shift;
             i < std::min((w.cell.i + 1) << shift, cells); ++i)
          for (int j = w.cell.j << shift;
               j < std::min((w.cell.j + 1) << shift, cells); ++j) {
            const Worldgen::PlanetCell cell{w.cell.face, i, j, level};
            const float distance = distance_to(cell);
            if (distance - extent * cell_width <= reach)
              place(cell, distance);
          }
      }
    }
  // The terrain's own regions, while its stage is on: off, it keeps (hidden)
  // those it has
  if (!terrain.enabled) {
    wanted.clear();
    is_wanted.clear();
    for (const auto& entry : state.regions) {
      const Worldgen::PlanetCell cell = _CellOf(entry.first);
      wanted.push_back({cell, distance_to(cell)});
      is_wanted[entry.first] = true;
    }
  }
  state.placing.Finish([&](uint64_t key, RegionBuild build) {
    const Worldgen::Region& region = build.region;
    // One built with edits since changed is started again
    if (placement_cells.count(key) &&
        _SameEdits(build.edits, state.edits.get(), key)) {
      const Worldgen::PlanetCell cell = _CellOf(key);
      auto [entity, transform] = ecs.CreateEntity(
          "Objects " + std::to_string(cell.face) + "/" +
              std::to_string(cell.level) + "/" + std::to_string(cell.i) + "/" +
              std::to_string(cell.j),
          volume);
      transform.position_ =
          glm::vec3(region.origin[0], region.origin[1], region.origin[2]);
      ecs.Add<ProceduralInstanceComponent>(entity, volume, "objects");
      PlanetState::ObjectCell& placed = state.placements[key];
      placed.entity = entity;
      placed.orphan_edits = build.orphan_edits;
      glm::vec3 low(std::numeric_limits<float>::max());
      glm::vec3 high(std::numeric_limits<float>::lowest());
      for (const Worldgen::ObjectInstances& kind : region.objects)
        for (const auto& matrix : kind.matrices) {
          const glm::vec3 origin(matrix[12], matrix[13], matrix[14]);
          low = glm::min(low, origin);
          high = glm::max(high, origin);
        }
      if (low.x <= high.x) {
        placed.centre = transform.position_ + 0.5f * (low + high);
        placed.radius = 0.5f * glm::length(high - low);
      }
      // Made a few per frame, with the other regions' objects
      if (!build.region.objects.empty()) {
        auto& work = state.making.emplace_back(entity, state.instance_content,
                                               std::move(build));
        work.progress.stage = "objects";
        work.progress.cell = key;
      } else
        retire(key);
    }
  });
  // Those no longer wanted fade out once their objects are all past their
  // reach (the cells' test goes by their centres, roughly), then go
  std::erase_if(state.placements, [&](auto& entry) {
    if (placement_cells.count(entry.first))
      return false;
    const PlanetState::ObjectCell& placed = entry.second;
    const float reach = reach_of(_CellOf(entry.first).level);
    if (placed.radius >= 0.0f && std::isfinite(reach) &&
        viewer_distance(placed.centre) - placed.radius <= reach)
      return false;
    _FadeOut(ecs, placed.entity, generator.fade_time);
    return true;
  });
  for (auto it = state.old_placements.begin();
       it != state.old_placements.end();) {
    if (placement_cells.count(it->first) || state.placements.count(it->first)) {
      ++it;
      continue;
    }
    _FadeOut(ecs, it->second.entity, generator.fade_time);
    it = state.old_placements.erase(it);
  }

  // Within the frame budget: the objects of regions loaded before, then
  // finished builds still wanted (their terrain and buildings; their objects
  // join the queue)
  const FrameBudget::Spend spend;
  const auto start = Clock::now();
  const std::function<bool()> more = FrameBudget::More;
  size_t objects_made = 0;
  while (!state.making.empty() && more()) {
    auto& work = state.making.front();
    const size_t before = work.progress.made;
    std::vector<Entity> made;
    const bool done =
        !ecs.Reg().valid(work.region) ||
        RegionInstancer::InstanceObjects(
            ecs, work.build.region, *work.content, work.region, root_,
            &state.textures, &work.build.objects, work.progress, more, &made,
            work.build.tags.empty() ? nullptr : &work.build.tags);
    _FadeIn(ecs, made, generator.fade_time);
    objects_made += work.progress.made - before;
    if (done) {
      // A cell made again replaces its old one now its objects show
      for (const auto& [key, old] : state.old_placements)
        if (const auto now = state.placements.find(key);
            now != state.placements.end() && now->second.entity == work.region) {
          retire(key);
          break;
        }
      state.making.pop_front();
    }
  }
  const auto make_region = [&](uint64_t key, RegionBuild build) {
    const Worldgen::Region& region = build.region;
    const Worldgen::PlanetCell cell = _CellOf(key);
    if (is_wanted.count(key)) {
      // The terrain's content has no objects: the placement regions place them
      const std::shared_ptr<const Worldgen::RegionContent>& content_of =
          state.content;
      const Worldgen::RegionContent& content = *content_of;
      const std::string name =
          "Region " + std::to_string(cell.face) + "/" +
          std::to_string(cell.level) + "/" + std::to_string(cell.i) + "/" +
          std::to_string(cell.j);
      auto [entity, transform] = ecs.CreateEntity(name, volume);
      transform.position_ =
          glm::vec3(region.origin[0], region.origin[1], region.origin[2]);
      ecs.Add<ProceduralInstanceComponent>(entity, volume, "terrain");
      // Deep enough to cover the height differences between levels
      const float skirt_depth = static_cast<float>(32 << cell.level) * 0.25f;
      auto result =
          RegionInstancer::Instance(ecs, region, content, entity,
                                    looks_[volume].material, root_,
                                    skirt_depth, &state.textures,
                                    &build.objects, false);
      // Its buildings fade in and out with it
      for (const Entity building : RegionInstancer::InstanceBuildings(
               ecs, build.pieces, entity, root_, &state.textures))
        ecs.Get<ProceduralInstanceComponent>(building).stage_ = "buildings";
      _FadeIn(ecs, {entity}, generator.fade_time);
      state.regions[key] = {entity, std::move(result.terrain_mesh)};
      if (!build.region.objects.empty())
        state.making.push_back({entity, content_of, std::move(build)});
    }
  };
  state.building.Finish(make_region, more);
  // One object or region can still take longer than the budget on its own
  if (const float ms = _Milliseconds(start); ms > 4.0f * FrameBudget::Total())
    Logger::getInstance().Log(
        LogLevel::Warning,
        "[ProcWorld] Region making took " + std::to_string(ms) + " ms (" +
            std::to_string(objects_made) + " objects, " +
            std::to_string(state.making.size()) + " regions still making)");

  // Unload the regions no longer wanted once every wanted region over their
  // ground is loaded and shown (two half-faded layers would leave holes):
  // they fade out over them
  std::erase_if(state.leaving, [&](const LoadedRegion& region) {
    return !ecs.Reg().valid(region.entity);
  });
  std::erase_if(state.regions, [&](auto& entry) {
    if (is_wanted.count(entry.first))
      return false;
    const Worldgen::PlanetCell cell = _CellOf(entry.first);
    for (const Wanted& w : wanted) {
      if (!_Overlap(cell, w.cell))
        continue;
      const auto loaded = state.regions.find(_CellKey(w.cell));
      if (loaded == state.regions.end() ||
          !_Shown(ecs, loaded->second.entity))
        return false;
    }
    _FadeOut(ecs, entry.second.entity, generator.fade_time);
    state.leaving.push_back(std::move(entry.second));
    return true;
  });

  // Start the missing ones, those seen soonest first: terrain and herds by
  // their distance, object cells by how far they are inside their reach (so
  // far terrain doesn't hold up the objects about to show)
  std::erase_if(wanted, [&](const Wanted& w) {
    const uint64_t key = _CellKey(w.cell);
    return state.regions.count(key) > 0 || state.building.Contains(key);
  });
  std::vector<std::pair<uint64_t, Worldgen::PlanetCell>> missing_herds;
  for (const auto& [key, cell] : herd_cells)
    if (!state.herds.count(key) && !state.herd_building.Contains(key))
      missing_herds.emplace_back(key, cell);
  std::vector<Placement> missing;
  for (const auto& [key, placement] : placement_cells)
    if (!state.placements.count(key) && !state.placing.Contains(key))
      missing.push_back(placement);
  const size_t jobs = generator.worker_jobs > 0
                          ? static_cast<size_t>(generator.worker_jobs)
                          : Jobs::Workers();
  size_t started = 0;
  const auto busy = [&] {
    return state.building.Size() + state.herd_building.Size() +
               state.placing.Size() >=
           jobs;
  };
  // Whether the stage gathers a region's buildings
  const auto building_reach = [&](const Wanted& w) {
    const PCG::Stage& buildings = generator.buildings;
    return buildings.enabled && (buildings.generation_radius <= 0.0f ||
                                 w.distance <= buildings.generation_radius);
  };
  Paging::StartQueue starts;
  for (const Wanted& w : wanted)
    starts.Add(w.distance, [&, w] {
      std::shared_ptr<const Worldgen::RegionContent> content = state.content;
      int centre[3];
      Worldgen::PlanetGrid::CellCentre(w.cell, radius, centre);
      const int level = w.cell.level;
      // The region's buildings are made here too, off the main thread
      state.building.Start(
          _CellKey(w.cell),
          [content,
           buildings =
               level <= generator.buildings.grid && building_reach(w)
                   ? state.buildings
                   : nullptr,
           x = centre[0], y = centre[1], z = centre[2], level, root = root_,
           archetypes = state.archetypes,
           &procmodel = EngineContext::PCG().GetProcModel()] {
            RegionBuild build{Worldgen::BuildRegion(*content, x, y, z, level)};
            build.objects = RegionInstancer::GenerateObjects(
                build.region, *content, false, archetypes, procmodel, nullptr);
            if (buildings)
              build.pieces = RegionInstancer::GatherBuildings(build.region,
                                                              *buildings, root);
            return build;
          });
      started++;
    });
  for (const auto& [key, cell] : missing_herds)
    starts.Add(distance_to(cell), [&, key = key, cell = cell] {
      int centre[3];
      Worldgen::PlanetGrid::CellCentre(cell, radius, centre);
      state.herd_building.Start(
          key, [content = state.herd_content, x = centre[0], y = centre[1],
                z = centre[2], level = cell.level,
                archetypes = state.archetypes, planet = state.textures,
                &procmodel = EngineContext::PCG().GetProcModel()] {
            RegionBuild build{Worldgen::BuildRegion(*content, x, y, z, level)};
            build.objects = RegionInstancer::GenerateObjects(
                build.region, *content, true, archetypes, procmodel, &planet);
            return build;
          });
    });
  for (const Placement& placement : missing) {
    const float reach = reach_of(placement.cell.level);
    starts.Add(
        placement.distance - (std::isfinite(reach) ? reach : 0.0f),
        [&, placement] {
          int centre[3];
          Worldgen::PlanetGrid::CellCentre(placement.cell, radius, centre);
          // Its edits, copied: the layer can change while it builds
          const uint64_t key = _CellKey(placement.cell);
          const PCG::CellEdits* edits =
              state.edits ? state.edits->Find("objects", key) : nullptr;
          state.placing.Start(
              key,
              [content = state.object_content,
               instancing = state.instance_content,
               edit_objects = state.edit_objects,
               edits = edits ? *edits : PCG::CellEdits(),
               x = centre[0], y = centre[1], z = centre[2],
               level = placement.cell.level,
               archetypes = state.object_archetypes,
               &procmodel = EngineContext::PCG().GetProcModel()] {
                RegionBuild build{
                    Worldgen::BuildRegion(*content, x, y, z, level)};
                // Keyed even unedited, so the first edit finds its object
                build.orphan_edits = _EditObjects(
                    edits, *instancing, *edit_objects, build.region,
                    build.tags);
                build.edits = edits;
                build.objects = RegionInstancer::GenerateObjects(
                    build.region, *instancing, false, archetypes, procmodel,
                    nullptr, &build.tags);
                return build;
              });
        });
  }
  starts.Run(busy);
  // TODO: a herd cell that is also a wanted region is built twice

  // A stage that is off shows nothing of what it keeps
  const float fade = generator.fade_time;
  _ShowStage(ecs, state.sea, generator.sea.enabled, fade);
  for (const auto& entry : state.herds)
    _ShowStage(ecs, entry.second, herds.enabled, fade);
  for (const auto& entry : state.placements)
    _ShowStage(ecs, entry.second.entity, generator.objects.enabled, fade);
  // A region holds the terrain and its buildings
  for (const auto& entry : state.regions)
    if (const auto* transform =
            ecs.Reg().try_get<TransformComponent>(entry.second.entity))
      for (const Entity child : transform->children_) {
        const auto* instance =
            ecs.Reg().try_get<ProceduralInstanceComponent>(child);
        _ShowStage(ecs, child,
                   instance && instance->stage_ == "buildings"
                       ? generator.buildings.enabled
                       : terrain.enabled,
                   fade);
      }

  const bool was_loading =
      generator.queued_regions + generator.building_regions > 0;
  generator.loaded_regions = state.regions.size();
  generator.orphan_edits = 0;
  for (const auto& entry : state.placements)
    generator.orphan_edits += entry.second.orphan_edits;
  generator.building_regions = state.building.Size();
  generator.queued_regions = wanted.size() - started;
  if (was_loading &&
      generator.queued_regions + generator.building_regions == 0)
    Logger::getInstance().Log(
        LogLevel::Info, "[ProcWorld] " + generator.content_path + ": " +
                            std::to_string(generator.loaded_regions) +
                            " regions loaded");
}

}  // namespace ProcWorld
