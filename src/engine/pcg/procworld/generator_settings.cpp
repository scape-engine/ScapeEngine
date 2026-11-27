#include "generator_settings.h"

#include "engine/pcg/points/spawner_generator.h"
#include "engine/pcg/procworld/craft_generator.h"
#include "engine/pcg/procworld/planet_generator.h"
#include "engine/pcg/procworld/region_generator.h"
#include "engine/pcg/procworld/star_system_source.h"
#include "engine/scene/glm_json.h"

namespace ProcWorld {

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
    for (size_t i = 0; i < colours->size() && i < look.tile_colours.size(); ++i)
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

static nlohmann::json _WriteCreatures(const HerdSettings& herd) {
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

static void _ReadWater(const nlohmann::json& in,
                       Water::SurfaceSettings& water) {
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

static void _ReadCreatures(const nlohmann::json& in, HerdSettings& herd) {
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

static void _ReadStages(const nlohmann::json& in, PlanetGenerator& planet) {
  const auto stages = in.find("stages");
  if (stages == in.end())
    return;
  const auto read = [&](const char* name, PCG::Stage& stage) {
    const auto it = stages->find(name);
    if (it == stages->end())
      return nlohmann::json::object();
    PCG::ReadStage(*it, stage);
    return *it;
  };
  const nlohmann::json terrain = read("terrain", planet.terrain);
  planet.terrain.coarsest_level =
      terrain.value("coarsest_level", planet.terrain.coarsest_level);
  planet.terrain.split_distance =
      terrain.value("split_distance", planet.terrain.split_distance);
  read("buildings", planet.buildings);
  planet.objects.assets =
      read("objects", planet.objects).value("assets", planet.objects.assets);
  _ReadCreatures(read("herds", planet.herds), planet.herds.creatures);
  _ReadWater(read("sea", planet.sea), planet.sea.water);
}

nlohmann::json WriteSettings(const PCG::Generator& generator) {
  switch (generator.GetType()) {
    case PCG::GeneratorType::Region: {
      const auto& region = static_cast<const RegionGenerator&>(generator);
      return {{"type", "Region"},
              {"content", region.content_path},
              {"cell", {region.cell.x, region.cell.y, region.cell.z}},
              {"planet", region.planet_path},
              {"seed", region.seed},
              {"look", _WriteLook(region.look)}};
    }
    case PCG::GeneratorType::Planet: {
      const auto& planet = static_cast<const PlanetGenerator&>(generator);
      nlohmann::json terrain = PCG::WriteStage(planet.terrain);
      terrain["coarsest_level"] = planet.terrain.coarsest_level;
      terrain["split_distance"] = planet.terrain.split_distance;
      nlohmann::json objects = PCG::WriteStage(planet.objects);
      objects["assets"] = planet.objects.assets;
      nlohmann::json herds = PCG::WriteStage(planet.herds);
      herds["creatures"] = _WriteCreatures(planet.herds.creatures);
      nlohmann::json sea = PCG::WriteStage(planet.sea);
      sea["water"] = _WriteWater(planet.sea.water);
      return {{"type", "Planet"},
              {"content", planet.content_path},
              // By name, so a profile's overrides patch single settings
              {"stages",
               {{"terrain", terrain},
                {"buildings", PCG::WriteStage(planet.buildings)},
                {"objects", objects},
                {"herds", herds},
                {"sea", sea}}},
              {"worker_jobs", planet.worker_jobs},
              {"fade_time", planet.fade_time},
              {"planet", planet.planet_path},
              {"seed", planet.seed},
              {"biome", planet.biome},
              {"subtype", planet.subtype},
              {"size", planet.size},
              {"look", _WriteLook(planet.look)}};
    }
    case PCG::GeneratorType::Craft: {
      const auto& craft = static_cast<const CraftGenerator&>(generator);
      return {{"type", "Craft"},
              {"content", craft.content_path},
              {"seed", craft.seed},
              {"lawless", craft.lawless},
              {"legacy_colours", craft.legacy_colours}};
    }
    case PCG::GeneratorType::Spawner: {
      const auto& spawner =
          static_cast<const PCG::SpawnerGenerator&>(generator);
      nlohmann::json bindings = nlohmann::json::array();
      for (const auto& binding : spawner.bindings) {
        nlohmann::json out = {{"attribute", binding.attribute},
                              {"setting", binding.setting}};
        if (!binding.table.is_null())
          out["table"] = binding.table;
        bindings.push_back(std::move(out));
      }
      return {{"type", "Spawner"},
              {"source", spawner.source->Write()},
              {"template", spawner.template_profile},
              {"bindings", std::move(bindings)},
              {"edits_folder", spawner.edits_folder},
              {"activation",
               {{"distance", spawner.activation_distance},
                {"max_active", spawner.max_active}}}};
    }
    default:
      return nullptr;
  }
}

void ReadSettings(PCG::Generator& generator, const nlohmann::json& in) {
  switch (generator.GetType()) {
    case PCG::GeneratorType::Region: {
      auto* region = static_cast<RegionGenerator*>(&generator);
      region->content_path = in.value("content", region->content_path);
      region->planet_path = in.value("planet", region->planet_path);
      region->seed = in.value("seed", region->seed);
      _ReadLook(in, region->look);
      if (auto cell = in.find("cell"); cell != in.end())
        region->cell = {cell->at(0).get<int>(), cell->at(1).get<int>(),
                        cell->at(2).get<int>()};
      break;
    }
    case PCG::GeneratorType::Planet: {
      auto* planet = static_cast<PlanetGenerator*>(&generator);
      planet->content_path = in.value("content", planet->content_path);
      _ReadStages(in, *planet);
      planet->fade_time = in.value("fade_time", planet->fade_time);
      planet->worker_jobs = in.value("worker_jobs", planet->worker_jobs);
      planet->planet_path = in.value("planet", planet->planet_path);
      planet->seed = in.value("seed", planet->seed);
      planet->biome = in.value("biome", planet->biome);
      planet->subtype = in.value("subtype", planet->subtype);
      planet->size = in.value("size", planet->size);
      _ReadLook(in, planet->look);
      break;
    }
    case PCG::GeneratorType::Craft: {
      auto* craft = static_cast<CraftGenerator*>(&generator);
      craft->content_path = in.value("content", craft->content_path);
      craft->seed = in.value("seed", craft->seed);
      craft->lawless = in.value("lawless", craft->lawless);
      craft->legacy_colours = in.value("legacy_colours", craft->legacy_colours);
      break;
    }
    case PCG::GeneratorType::Spawner: {
      auto* spawner = static_cast<PCG::SpawnerGenerator*>(&generator);
      // Another type of source starts from its defaults
      if (const auto source = in.find("source"); source != in.end()) {
        if (source->value("type", std::string()) != spawner->source->Type()) {
          if (auto made = CreatePointSource(*source))
            spawner->source = std::move(made);
        } else {
          spawner->source->Read(*source);
        }
      }
      spawner->template_profile =
          in.value("template", spawner->template_profile);
      if (const auto bindings = in.find("bindings"); bindings != in.end()) {
        spawner->bindings.clear();
        for (const auto& binding : *bindings)
          spawner->bindings.push_back(
              {binding.value("attribute", std::string()),
               binding.value("setting", std::string()),
               binding.value("table", nlohmann::json())});
      }
      spawner->edits_folder = in.value("edits_folder", spawner->edits_folder);
      if (const auto it = in.find("activation"); it != in.end()) {
        spawner->activation_distance =
            it->value("distance", spawner->activation_distance);
        spawner->max_active = it->value("max_active", spawner->max_active);
      }
      break;
    }
    default:
      break;
  }
}

std::unique_ptr<PCG::Generator> CreateGenerator(const nlohmann::json& in) {
  const std::string type = in.value("type", std::string());
  std::unique_ptr<PCG::Generator> generator;
  if (type == "Planet")
    generator = std::make_unique<PlanetGenerator>();
  else if (type == "Craft")
    generator = std::make_unique<CraftGenerator>();
  else if (type == "Region")
    generator = std::make_unique<RegionGenerator>();
  else if (type == "Spawner")
    generator = std::make_unique<PCG::SpawnerGenerator>();
  else
    return nullptr;
  ReadSettings(*generator, in);
  return generator;
}

std::unique_ptr<PCG::PointSource> CreatePointSource(const nlohmann::json& in) {
  const std::string type = in.value("type", std::string());
  std::unique_ptr<PCG::PointSource> source;
  if (type == "Grid")
    source = std::make_unique<PCG::GridSource>();
  else if (type == "StarSystem")
    source = std::make_unique<StarSystemSource>();
  else
    return nullptr;
  source->Read(in);
  return source;
}

}  // namespace ProcWorld
