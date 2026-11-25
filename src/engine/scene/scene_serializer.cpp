// ---------------------------------------------------------------------------
// scene_serializer.cpp
// Scene format (version 1):
// { "version": 1, "entities": [ { "name", "parent": <index|-1>, "position",
//   "rotation", "euler", "scale", "<component>": {...} } ],
//   "editor_camera": { "position", "rotation", "euler", "move_speed" } }
// "editor_camera" is optional.
// Parents are always written before their children.
// ---------------------------------------------------------------------------

#include "scene_serializer.h"

#include <functional>
#include <vector>

#include "engine/context/engine_context.h"
#include "engine/core/logger.h"
#include "engine/ecs/components/fade_component.h"
#include "engine/ecs/components/procedural_instance_component.h"
#include "engine/ecs/components/volume_component.h"
#include "engine/ecs/world.h"
#include "engine/pcg/generator_resource.h"
#include "engine/pcg/procworld/craft_generator.h"
#include "engine/pcg/procworld/planet_generator.h"
#include "engine/pcg/procworld/region_generator.h"

namespace fs = std::filesystem;

constexpr int scene_version = 1;

/* ---------- glm ---------- */
namespace nlohmann {
template <>
struct adl_serializer<glm::vec3> {
  static void to_json(json& j, const glm::vec3& v) { j = {v.x, v.y, v.z}; }
  static void from_json(const json& j, glm::vec3& v) {
    v = glm::vec3(j.at(0).get<float>(), j.at(1).get<float>(),
                  j.at(2).get<float>());
  }
};

// Stored as w, x, y, z
template <>
struct adl_serializer<glm::quat> {
  static void to_json(json& j, const glm::quat& q) { j = {q.w, q.x, q.y, q.z}; }
  static void from_json(const json& j, glm::quat& q) {
    q = glm::quat(j.at(0).get<float>(), j.at(1).get<float>(),
                  j.at(2).get<float>(), j.at(3).get<float>());
  }
};
}  // namespace nlohmann

/* ---------- components (persisted fields only) ---------- */
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CameraComponent, enabled_, fov_,
                                                near_plane_, far_plane_)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    DirectionalLightComponent, enabled_, color_, intensity_, cast_shadows_,
    shadow_bias_, normal_bias_, shadow_resolution_, shadow_softness_)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    PointLightComponent, enabled_, color_, intensity_, range_, falloff_,
    cast_shadows_, shadow_bias_, shadow_resolution_, shadow_softness_)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    SpotlightComponent, enabled_, color_, intensity_, range_, falloff_,
    inner_angle_, outer_angle_, cast_shadows_, shadow_bias_, shadow_resolution_,
    shadow_softness_)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FadeComponent, visibility_,
                                                target_, duration_,
                                                destroy_when_hidden_)
// recapture_ is a one-shot request, not state
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    SkyLightComponent, enabled_, color_, intensity_, cast_shadows_,
    source_type_, cubemap_path_, source_cubemap_angle_, real_time_capture_,
    lower_hemisphere_is_solid_color_, lower_hemisphere_color_)

// TODO: graph generators (volumes keep only region generators)
// TODO: material overrides (materials currently come only from the glTF
// source)
// TODO: move world settings (sky, post-process) into ECS components so
// SceneSerializer covers them

/* ---------- helpers ---------- */
template <typename T>
static void _Write(ECS& world, Entity entity, nlohmann::json& out,
                   const char* key) {
  if (world.Has<T>(entity))
    out[key] = world.Get<T>(entity);
}

template <typename T>
static void _Read(ECS& world, Entity entity, const nlohmann::json& in,
                  const char* key) {
  if (auto it = in.find(key); it != in.end())
    world.Add<T>(entity, it->get<T>());
}

// Paths outside the project stay absolute
static std::string _Relative(const std::string& path, const fs::path& root) {
  if (path.empty())
    return path;
  std::error_code ec;
  const fs::path relative = fs::relative(path, root, ec);
  if (ec || relative.empty() || *relative.begin() == "..")
    return path;
  return relative.generic_string();
}

static std::string _Absolute(const std::string& path, const fs::path& root) {
  if (path.empty() || fs::path(path).is_absolute())
    return path;
  return (root / path).string();
}

static nlohmann::json _WriteLook(const TerrainMaterialData& look) {
  nlohmann::json colours = nlohmann::json::array();
  for (const glm::vec3& colour : look.tile_colours)
    colours.push_back(colour);
  return {{"tile_colours", colours},
          {"tile_images", look.tile_images},
          {"height_offset", look.height_offset},
          {"height_scale", look.height_scale},
          {"roughness", look.roughness},
          {"tile_noise",
           {look.tile_noise.x, look.tile_noise.y, look.tile_noise.z,
            look.tile_noise.w}}};
}

// Missing values keep the defaults
static void _ReadLook(const nlohmann::json& in, TerrainMaterialData& look) {
  auto it = in.find("look");
  if (it == in.end())
    return;
  if (auto colours = it->find("tile_colours"); colours != it->end()) {
    for (size_t i = 0;
         i < colours->size() && i < look.tile_colours.size(); ++i)
      look.tile_colours[i] = colours->at(i).get<glm::vec3>();
  }
  auto read_tiles = [&](const char* key, auto& values) {
    if (auto list = it->find(key); list != it->end())
      for (size_t i = 0; i < list->size() && i < values.size(); ++i)
        list->at(i).get_to(values[i]);
  };
  read_tiles("tile_images", look.tile_images);
  read_tiles("height_offset", look.height_offset);
  read_tiles("height_scale", look.height_scale);
  look.roughness = it->value("roughness", look.roughness);
  if (auto noise = it->find("tile_noise"); noise != it->end())
    look.tile_noise = {noise->at(0).get<float>(), noise->at(1).get<float>(),
                       noise->at(2).get<float>(), noise->at(3).get<float>()};
}

static nlohmann::json _WriteCreatures(const ProcWorld::HerdSettings& herd) {
  const AnimatorSettings& a = herd.animation;
  return {{"roam", herd.roam},
          {"walk_speed", herd.walk_speed},
          {"turn_rate", herd.turn_rate},
          {"roam_radius", herd.roam_radius},
          {"rest_min", herd.rest_min},
          {"rest_max", herd.rest_max},
          {"nav_reach", herd.nav_reach},
          {"roam_distance", herd.roam_distance},
          {"walk_clip", herd.walk_clip},
          {"rest_clip", herd.rest_clip},
          {"fade", herd.fade},
          {"forward", herd.forward},
          {"match_stride", herd.match_stride},
          {"walk_min_rate", herd.walk_min_rate},
          {"walk_max_rate", herd.walk_max_rate},
          {"keep_pace", herd.keep_pace},
          {"animation",
           {{"plant_feet", a.plant_feet},
            {"plant_distance", a.plant_distance},
            {"plant_reach", a.plant_reach},
            {"full_rate_distance", a.full_rate_distance},
            {"max_step", a.max_step},
            {"sample_distance", a.sample_distance}}}};
}

// Missing values keep the defaults
static nlohmann::json _WriteWater(const Water::SurfaceSettings& water) {
  const Worldgen::WaveCascadeSettings& c = water.cascades;
  return {{"base_scale", c.base_scale},
          {"cascades", c.count},
          {"resolution", c.resolution},
          {"min_texels", c.min_texels},
          {"gravity", c.gravity},
          {"patch_size", water.patch_size},
          {"grid_resolution", water.grid_resolution},
          {"colour", water.look.colour},
          {"roughness", water.look.roughness}};
}

static void _ReadWater(const nlohmann::json& in, Water::SurfaceSettings& water) {
  auto it = in.find("water");
  if (it == in.end())
    return;
  Worldgen::WaveCascadeSettings& c = water.cascades;
  c.base_scale = it->value("base_scale", c.base_scale);
  c.count = it->value("cascades", c.count);
  c.resolution = it->value("resolution", c.resolution);
  c.min_texels = it->value("min_texels", c.min_texels);
  c.gravity = it->value("gravity", c.gravity);
  water.patch_size = it->value("patch_size", water.patch_size);
  water.grid_resolution = it->value("grid_resolution", water.grid_resolution);
  water.look.colour = it->value("colour", water.look.colour);
  water.look.roughness = it->value("roughness", water.look.roughness);
}

static void _ReadCreatures(const nlohmann::json& in,
                           ProcWorld::HerdSettings& herd) {
  auto it = in.find("creatures");
  if (it == in.end())
    return;
  herd.roam = it->value("roam", herd.roam);
  herd.walk_speed = it->value("walk_speed", herd.walk_speed);
  herd.turn_rate = it->value("turn_rate", herd.turn_rate);
  herd.roam_radius = it->value("roam_radius", herd.roam_radius);
  herd.rest_min = it->value("rest_min", herd.rest_min);
  herd.rest_max = it->value("rest_max", herd.rest_max);
  herd.nav_reach = it->value("nav_reach", herd.nav_reach);
  herd.roam_distance = it->value("roam_distance", herd.roam_distance);
  herd.walk_clip = it->value("walk_clip", herd.walk_clip);
  herd.rest_clip = it->value("rest_clip", herd.rest_clip);
  herd.fade = it->value("fade", herd.fade);
  herd.forward = it->value("forward", herd.forward);
  herd.match_stride = it->value("match_stride", herd.match_stride);
  herd.walk_min_rate = it->value("walk_min_rate", herd.walk_min_rate);
  herd.walk_max_rate = it->value("walk_max_rate", herd.walk_max_rate);
  herd.keep_pace = it->value("keep_pace", herd.keep_pace);
  auto animation = it->find("animation");
  if (animation == it->end())
    return;
  AnimatorSettings& a = herd.animation;
  a.plant_feet = animation->value("plant_feet", a.plant_feet);
  a.plant_distance = animation->value("plant_distance", a.plant_distance);
  a.plant_reach = animation->value("plant_reach", a.plant_reach);
  a.full_rate_distance =
      animation->value("full_rate_distance", a.full_rate_distance);
  a.max_step = animation->value("max_step", a.max_step);
  a.sample_distance = animation->value("sample_distance", a.sample_distance);
}

// A volume and its generator settings (resources are runtime only)
static nlohmann::json _WriteVolume(const VolumeComponent& volume) {
  nlohmann::json out = {{"bounds_min", volume.bounds_min},
                        {"bounds_max", volume.bounds_max},
                        {"resolution", volume.resolution},
                        {"lod_level", volume.lod_level}};
  auto generator =
      EngineContext::resourceManager().GetResourceAs<PCG::GeneratorResource>(
          volume.generator_id);
  if (generator &&
      generator->GetGeneratorType() == PCG::GeneratorType::Region) {
    const auto& region =
        static_cast<const ProcWorld::RegionGenerator&>(*generator->Get());
    out["generator"] = {
        {"type", "Region"},
        {"content", region.content_path},
        {"cell", {region.cell.x, region.cell.y, region.cell.z}},
        {"planet", region.planet_path},
        {"seed", region.seed},
        {"look", _WriteLook(region.look)}};
  }
  if (generator &&
      generator->GetGeneratorType() == PCG::GeneratorType::Planet) {
    const auto& planet =
        static_cast<const ProcWorld::PlanetGenerator&>(*generator->Get());
    out["generator"] = {{"type", "Planet"},
                        {"content", planet.content_path},
                        {"finest_level", planet.finest_level},
                        {"coarsest_level", planet.coarsest_level},
                        {"split_distance", planet.split_distance},
                        {"view_distance", planet.view_distance},
                        {"objects", planet.objects},
                        {"building_level", planet.building_level},
                        {"herd_level", planet.herd_level},
                        {"worker_jobs", planet.worker_jobs},
                        {"fade_time", planet.fade_time},
                        {"planet", planet.planet_path},
                        {"seed", planet.seed},
                        {"biome", planet.biome},
                        {"subtype", planet.subtype},
                        {"size", planet.size},
                        {"look", _WriteLook(planet.look)},
                        {"creatures", _WriteCreatures(planet.creatures)},
                        {"water", _WriteWater(planet.water)}};
  }
  if (generator &&
      generator->GetGeneratorType() == PCG::GeneratorType::Craft) {
    const auto& craft =
        static_cast<const ProcWorld::CraftGenerator&>(*generator->Get());
    out["generator"] = {{"type", "Craft"},
                        {"content", craft.content_path},
                        {"seed", craft.seed},
                        {"lawless", craft.lawless},
                        {"legacy_colours", craft.legacy_colours}};
  }
  return out;
}

static void _ReadVolume(VolumeComponent& volume, const nlohmann::json& in) {
  volume.bounds_min = in.value("bounds_min", volume.bounds_min);
  volume.bounds_max = in.value("bounds_max", volume.bounds_max);
  volume.resolution = in.value("resolution", volume.resolution);
  volume.lod_level = in.value("lod_level", volume.lod_level);
  auto it = in.find("generator");
  if (it == in.end())
    return;
  const std::string type = it->value("type", std::string());

  if (type == "Planet") {
    auto planet = std::make_unique<ProcWorld::PlanetGenerator>();
    planet->content_path = it->value("content", std::string());
    planet->finest_level = it->value("finest_level", planet->finest_level);
    planet->coarsest_level =
        it->value("coarsest_level", planet->coarsest_level);
    planet->split_distance =
        it->value("split_distance", planet->split_distance);
    planet->view_distance =
        it->value("view_distance", planet->view_distance);
    planet->objects = it->value("objects", planet->objects);
    planet->building_level =
        it->value("building_level", planet->building_level);
    planet->herd_level = it->value("herd_level", planet->herd_level);
    planet->fade_time = it->value("fade_time", planet->fade_time);
    planet->worker_jobs = it->value("worker_jobs", planet->worker_jobs);
    planet->planet_path = it->value("planet", planet->planet_path);
    planet->seed = it->value("seed", planet->seed);
    planet->biome = it->value("biome", planet->biome);
    planet->subtype = it->value("subtype", planet->subtype);
    planet->size = it->value("size", planet->size);
    _ReadLook(*it, planet->look);
    _ReadCreatures(*it, planet->creatures);
    _ReadWater(*it, planet->water);
    auto [id, resource] =
        EngineContext::resourceManager().Create<PCG::GeneratorResource>(
            "Planet");
    resource->SetGenerator(std::move(planet));
    volume.generator_id = id;
    volume.dirty = true;
    return;
  }
  if (type == "Craft") {
    auto craft = std::make_unique<ProcWorld::CraftGenerator>();
    craft->content_path = it->value("content", std::string());
    craft->seed = it->value("seed", craft->seed);
    craft->lawless = it->value("lawless", craft->lawless);
    craft->legacy_colours =
        it->value("legacy_colours", craft->legacy_colours);
    auto [id, resource] =
        EngineContext::resourceManager().Create<PCG::GeneratorResource>(
            "Craft");
    resource->SetGenerator(std::move(craft));
    volume.generator_id = id;
    volume.dirty = true;
    return;
  }
  if (type != "Region")
    return;

  auto region = std::make_unique<ProcWorld::RegionGenerator>();
  region->content_path = it->value("content", std::string());
  region->planet_path = it->value("planet", region->planet_path);
  region->seed = it->value("seed", region->seed);
  _ReadLook(*it, region->look);
  if (auto cell = it->find("cell"); cell != it->end())
    region->cell = {cell->at(0).get<int>(), cell->at(1).get<int>(),
                    cell->at(2).get<int>()};
  auto [id, resource] =
      EngineContext::resourceManager().Create<PCG::GeneratorResource>(
          "Region");
  resource->SetGenerator(std::move(region));
  volume.generator_id = id;
  volume.dirty = true;
}

/* ---------- serialize ---------- */
nlohmann::json SceneSerializer::Serialize(ECS& world, const fs::path& root) {
  nlohmann::json entities = nlohmann::json::array();

  // Depth-first, so a parent is always written before its children and
  // children keep their order
  std::function<void(Entity, int)> write = [&](Entity entity, int parent) {
    // Made by a PCG system: remade from its source on load
    if (world.Has<ProceduralInstanceComponent>(entity))
      return;
    const TransformComponent& transform = world.Get<TransformComponent>(entity);
    const int index = static_cast<int>(entities.size());

    nlohmann::json out = {
        {"name", transform.name_},          {"parent", parent},
        {"position", transform.position_},  {"rotation", transform.rotation_},
        {"euler", transform.euler_angles_}, {"scale", transform.scale_}};

    _Write<CameraComponent>(world, entity, out, "camera");
    _Write<DirectionalLightComponent>(world, entity, out, "directional_light");
    _Write<PointLightComponent>(world, entity, out, "point_light");
    _Write<SpotlightComponent>(world, entity, out, "spotlight");
    _Write<FadeComponent>(world, entity, out, "fade");

    if (world.Has<SkyLightComponent>(entity)) {
      SkyLightComponent sky = world.Get<SkyLightComponent>(entity);
      sky.cubemap_path_ = _Relative(sky.cubemap_path_, root);
      out["sky_light"] = sky;
    }

    if (world.Has<VolumeComponent>(entity))
      out["volume"] = _WriteVolume(world.Get<VolumeComponent>(entity));

    if (world.Has<MeshRendererComponent>(entity)) {
      const auto& renderer = world.Get<MeshRendererComponent>(entity);
      out["mesh_renderer"] = {{"source", renderer.source_},
                              {"mesh_index", renderer.mesh_index_},
                              {"enabled", renderer.enabled_}};
    }

    entities.push_back(std::move(out));

    // Copy: the reference above may not outlive the recursion
    const std::vector<Entity> children = transform.children_;
    for (Entity child : children)
      write(child, index);
  };

  // Roots oldest first (views iterate newest first), so loading recreates
  // them in the same order
  std::vector<Entity> roots;
  for (auto [entity, transform] : world.View<TransformComponent>().each()) {
    if (transform.parent_ == entt::null)
      roots.push_back(entity);
  }
  for (auto it = roots.rbegin(); it != roots.rend(); ++it)
    write(*it, -1);

  return {{"version", scene_version}, {"entities", std::move(entities)}};
}

/* ---------- deserialize ---------- */
bool SceneSerializer::Deserialize(ECS& world, const nlohmann::json& scene,
                                  const fs::path& root) {
  if (!scene.is_object() || scene.value("version", 0) != scene_version) {
    Logger::getInstance().Log(LogLevel::Error,
                              "[SceneSerializer] Unsupported scene version");
    return false;
  }

  world.Clear();
  try {
    std::vector<Entity> created;
    for (const nlohmann::json& in : scene.at("entities")) {
      auto [entity, transform] =
          world.CreateEntity(in.value("name", std::string("Entity")));
      transform.position_ = in.value("position", transform.position_);
      transform.rotation_ = in.value("rotation", transform.rotation_);
      transform.euler_angles_ = in.value("euler", transform.euler_angles_);
      transform.scale_ = in.value("scale", transform.scale_);

      // Parents are written first, so the index is already created
      const int parent = in.value("parent", -1);
      if (parent >= 0 && parent < static_cast<int>(created.size()))
        world.SetParent(entity, created[parent]);

      _Read<CameraComponent>(world, entity, in, "camera");
      _Read<DirectionalLightComponent>(world, entity, in, "directional_light");
      _Read<PointLightComponent>(world, entity, in, "point_light");
      _Read<SpotlightComponent>(world, entity, in, "spotlight");
      _Read<FadeComponent>(world, entity, in, "fade");

      if (auto it = in.find("sky_light"); it != in.end()) {
        auto& sky =
            world.Add<SkyLightComponent>(entity, it->get<SkyLightComponent>());
        sky.cubemap_path_ = _Absolute(sky.cubemap_path_, root);
      }

      if (auto it = in.find("volume"); it != in.end())
        _ReadVolume(world.Add<VolumeComponent>(entity), *it);

      if (auto it = in.find("mesh_renderer"); it != in.end()) {
        auto& renderer = world.Add<MeshRendererComponent>(entity);
        renderer.source_ = it->value("source", std::string());
        renderer.mesh_index_ = it->value("mesh_index", 0u);
        renderer.enabled_ = it->value("enabled", true);
      }

      created.push_back(entity);
    }
  } catch (const nlohmann::json::exception& e) {
    Logger::getInstance().Log(
        LogLevel::Error,
        "[SceneSerializer] Invalid scene: " + std::string(e.what()));
    world.Clear();
    return false;
  }
  return true;
}

/* ---------- editor camera ---------- */
void SceneSerializer::WriteEditorCamera(nlohmann::json& scene,
                                        const TransformComponent& camera,
                                        float move_speed) {
  scene["editor_camera"] = {{"position", camera.position_},
                            {"rotation", camera.rotation_},
                            {"euler", camera.euler_angles_},
                            {"move_speed", move_speed}};
}

bool SceneSerializer::ReadEditorCamera(const nlohmann::json& scene,
                                       TransformComponent& camera,
                                       float& move_speed) {
  auto it = scene.find("editor_camera");
  if (it == scene.end() || !it->is_object())
    return false;
  try {
    const glm::vec3 position = it->at("position").get<glm::vec3>();
    const glm::quat rotation = it->at("rotation").get<glm::quat>();
    const glm::vec3 euler = it->at("euler").get<glm::vec3>();
    move_speed = it->value("move_speed", move_speed);
    camera.position_ = position;
    camera.rotation_ = rotation;
    camera.euler_angles_ = euler;
    camera.modified_ = true;
  } catch (const nlohmann::json::exception& e) {
    Logger::getInstance().Log(
        LogLevel::Error,
        "[SceneSerializer] Invalid editor camera: " + std::string(e.what()));
    return false;
  }
  return true;
}
