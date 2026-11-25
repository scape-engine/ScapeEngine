#include "generator_properties.h"

#include <imgui.h>

#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>

#include "editor/events.h"
#include "editor/gui/components/im_components.h"
#include "editor/gui/components/inspectable_components.h"
#include "editor/runtime/runtime.h"
#include "engine/context/engine_context.h"
#include "engine/core/logger.h"
#include "engine/ecs/world.h"
#include "engine/pcg/generator_profile.h"
#include "engine/pcg/generator_resource.h"
#include "engine/pcg/procworld/generator_settings.h"
#include "engine/pcg/procworld/craft_generator.h"
#include "engine/pcg/procworld/planet_generator.h"
#include "engine/pcg/procworld/region_generator.h"
#include "engine/pcg/stage.h"

namespace GeneratorProperties {

// Marks the volumes using the generator for a rebuild
static void _RequestRebuild(ResourceID id) {
  for (auto [entity, volume] : ECS::Main().View<VolumeComponent>().each()) {
    if (volume.generator_id == id)
      volume.dirty = true;
  }
}

// Tile colours and material settings; they apply without a rebuild
static void _DrawTerrainLook(TerrainMaterialData& look) {
  if (!IMComponents::Foldout("Terrain Look"))
    return;
  ImGui::SliderFloat("Roughness", &look.roughness, 0.04f, 1.0f);
  ImGui::DragFloat4("Tile Noise", &look.tile_noise.x, 0.001f);
  ImGui::TextDisabled("Sample frequencies, second sample weight, offset");
  ImGui::TextDisabled("Tiles 0-15: groups of steep (even) and flat (odd)");
  for (int i = 0; i < TerrainMaterialData::tile_count; ++i) {
    ImGui::PushID(i);
    ImGui::ColorEdit3("##Colour", &look.tile_colours[i].x,
                      ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    if (IMComponents::Foldout(("Tile " + std::to_string(i) + " " +
                               look.tile_images[i] + "###Tile")
                                  .c_str())) {
      char path[512];
      std::snprintf(path, sizeof(path), "%s", look.tile_images[i].c_str());
      if (ImGui::InputText("Image", path, sizeof(path),
                           ImGuiInputTextFlags_EnterReturnsTrue))
        look.tile_images[i] = path;
      ImGui::TextDisabled("Relative to the project: RGB colour, alpha height");
      ImGui::DragFloat("Height Offset", &look.height_offset[i], 0.01f);
      ImGui::DragFloat("Height Scale", &look.height_scale[i], 0.01f);
    }
    ImGui::PopID();
  }
}

//
//  REGION PROPERTIES
//
static void _DrawRegion(ResourceID id) {
  auto resource = EngineContext::resourceManager()
                      .GetResourceAs<PCG::GeneratorResource>(
                          id);
  if (!resource || !resource->Get() ||
      resource->GetGeneratorType() != PCG::GeneratorType::Region) {
    ImGui::TextDisabled("The generator no longer exists");
    return;
  }
  auto& region = static_cast<ProcWorld::RegionGenerator&>(*resource->Get());


  bool changed = false;
  char path[512];
  std::snprintf(path, sizeof(path), "%s", region.content_path.c_str());
  if (ImGui::InputText("Content", path, sizeof(path),
                       ImGuiInputTextFlags_EnterReturnsTrue)) {
    region.content_path = path;
    changed = true;
  }
  ImGui::TextDisabled("JSON region content, relative to the project");

  changed |= ImGui::InputInt3("Cell", &region.cell.x,
                              ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::TextDisabled("Planet-space cell centre; zero: above the north pole");

  std::snprintf(path, sizeof(path), "%s", region.planet_path.c_str());
  if (ImGui::InputText("Planet", path, sizeof(path),
                       ImGuiInputTextFlags_EnterReturnsTrue)) {
    region.planet_path = path;
    changed = true;
  }
  ImGui::TextDisabled("JSON planet content, optional: its buildings");
  ImGui::InputScalar("Seed", ImGuiDataType_U64, &region.seed);
  changed |= ImGui::IsItemDeactivatedAfterEdit();

  changed |= ImGui::Button("Regenerate");

  _DrawTerrainLook(region.look);

  ImGui::Separator();
  if (!region.error.empty())
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                       region.error.c_str());
  else
    ImGui::Text("%zu vertices, %zu objects, %.1f ms", region.vertex_count,
                region.instance_count, region.build_ms);

  if (changed)
    _RequestRebuild(id);
}

//
//  CRAFT PROPERTIES
//
static void _DrawCraft(ResourceID id) {
  auto resource = EngineContext::resourceManager()
                      .GetResourceAs<PCG::GeneratorResource>(
                          id);
  if (!resource || !resource->Get() ||
      resource->GetGeneratorType() != PCG::GeneratorType::Craft) {
    ImGui::TextDisabled("The generator no longer exists");
    return;
  }
  auto& craft = static_cast<ProcWorld::CraftGenerator&>(*resource->Get());


  bool changed = false;
  char path[512];
  std::snprintf(path, sizeof(path), "%s", craft.content_path.c_str());
  if (ImGui::InputText("Content", path, sizeof(path),
                       ImGuiInputTextFlags_EnterReturnsTrue)) {
    craft.content_path = path;
    changed = true;
  }
  ImGui::TextDisabled("JSON craft content, relative to the project");
  ImGui::InputScalar("Seed", ImGuiDataType_U64, &craft.seed);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  changed |= ImGui::Checkbox("Lawless", &craft.lawless);
  ImGui::TextDisabled("Some craft change class at the content's chances");
  changed |= ImGui::Checkbox("Legacy colours", &craft.legacy_colours);
  ImGui::TextDisabled("Palettes drawn the older way, as old saves keep");

  changed |= ImGui::Button("Regenerate");

  ImGui::Separator();
  if (!craft.error.empty())
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                       craft.error.c_str());
  else
    ImGui::Text("%zu craft, %zu missing, %.1f ms", craft.craft_count,
                craft.missing_count, craft.build_ms);

  if (changed)
    _RequestRebuild(id);
}

//
//  PLANET PROPERTIES
//

// A stage's foldout, titled by its label and type, with its common settings;
// true while open. `grid_hint` and `radius_hint` say what the grid and the
// radius mean for it (null: it has none). With `rebuild`, editing them
// regenerates the planet
static bool _BeginStage(PCG::Stage& stage, const char* id,
                        const char* grid_hint, int grid_min, int grid_max,
                        const char* radius_hint, bool rebuild, bool& changed) {
  const std::string title =
      (stage.label.empty() ? std::string("Unnamed") : stage.label) + "  -  " +
      PCG::StageTypeName(stage.type) + (stage.enabled ? "" : "  (off)") +
      "###" + id;
  if (!IMComponents::Foldout(title.c_str()))
    return false;
  ImGui::PushID(id);
  bool edited = ImGui::Checkbox("Enabled", &stage.enabled);
  char label[128];
  std::snprintf(label, sizeof(label), "%s", stage.label.c_str());
  if (ImGui::InputText("Label", label, sizeof(label)))
    stage.label = label;
  ImGui::TextDisabled("Your name for it; nothing reads it");
  if (grid_hint) {
    edited |= ImGui::SliderInt("Grid", &stage.grid, grid_min, grid_max);
    ImGui::TextDisabled("%s", grid_hint);
  }
  if (radius_hint) {
    ImGui::DragFloat("Generation Radius", &stage.generation_radius, 16.0f,
                     0.0f, 1.0e6f, "%.0f m");
    edited |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::TextDisabled("%s", radius_hint);
  }
  ImGui::PopID();
  if (rebuild)
    changed |= edited;
  return true;
}

static void _DrawPlanet(ResourceID id) {
  auto resource = EngineContext::resourceManager()
                      .GetResourceAs<PCG::GeneratorResource>(
                          id);
  if (!resource || !resource->Get() ||
      resource->GetGeneratorType() != PCG::GeneratorType::Planet) {
    ImGui::TextDisabled("The generator no longer exists");
    return;
  }
  auto& planet = static_cast<ProcWorld::PlanetGenerator&>(*resource->Get());


  bool changed = false;
  char path[512];
  std::snprintf(path, sizeof(path), "%s", planet.content_path.c_str());
  if (ImGui::InputText("Content", path, sizeof(path),
                       ImGuiInputTextFlags_EnterReturnsTrue)) {
    planet.content_path = path;
    changed = true;
  }
  ImGui::TextDisabled("JSON region content, relative to the project");

  std::snprintf(path, sizeof(path), "%s", planet.planet_path.c_str());
  if (ImGui::InputText("Planet", path, sizeof(path),
                       ImGuiInputTextFlags_EnterReturnsTrue)) {
    planet.planet_path = path;
    changed = true;
  }
  ImGui::TextDisabled("JSON planet content, optional: terrain, radius, colours");
  ImGui::InputScalar("Seed", ImGuiDataType_U64, &planet.seed);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::InputInt("Biome", &planet.biome);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::InputInt("Subtype", &planet.subtype);
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::InputInt("Size Class", &planet.size);
  changed |= ImGui::IsItemDeactivatedAfterEdit();

  ImGui::SliderInt("Worker Jobs", &planet.worker_jobs, 0, 32);
  ImGui::TextDisabled("Regions building at once; 0: one per core but one");
  ImGui::DragFloat("Fade Time", &planet.fade_time, 0.01f, 0.0f, 10.0f, "%.2f s");
  ImGui::TextDisabled("Regions' content fades in and out; 0: at once");

  changed |= ImGui::Button("Regenerate");

  _DrawTerrainLook(planet.look);

  if (IMComponents::Foldout("Weather")) {
    if (!planet.weather_events) {
      ImGui::TextDisabled("The planet has no weather events");
    } else if (!planet.weather_event) {
      ImGui::Text("Clear, next event in %.0f s", planet.weather_event_change);
    } else {
      ImGui::Text("%s: kind %d, intensity %.2f",
                  planet.weather_event_warning ? "Warning" : "Event",
                  planet.weather_event_kind, planet.weather_event_intensity);
      ImGui::Text("Strength %.0f%%, next stage in %.0f s",
                  planet.weather_event_strength * 100.0f,
                  planet.weather_event_change);
    }
    ImGui::TextDisabled("Schedule and kinds come from the planet content");
  }

  if (IMComponents::Foldout("Stages", true)) {
    ImGui::TextDisabled(
        "What the planet makes of its regions. Level L's cells are 32 << L "
        "m wide; a radius of 0 leaves the stage to its own limits");

    // Loading settings apply from the next frame, no rebuild needed
    ProcWorld::TerrainStage& terrain = planet.terrain;
    if (_BeginStage(terrain, "Terrain", "The finest level", 0, 5,
                    "Regions farther are not loaded; 0: no limit", false,
                    changed)) {
      ImGui::SliderInt("Coarsest Level", &terrain.coarsest_level, terrain.grid,
                       9);
      ImGui::SliderFloat("Split Distance", &terrain.split_distance, 0.5f, 8.0f);
      ImGui::TextDisabled("A region splits when nearer than this many widths");
    }

    _BeginStage(planet.buildings, "Buildings",
                "Placed on the terrain's regions of this level or finer", 0, 9,
                "Only on regions this near; 0: as far as the terrain", true,
                changed);

    if (_BeginStage(planet.objects, "Objects", nullptr, 0, 0,
                    "No kind places farther; 0: each as far as it draws", false,
                    changed)) {
      ImGui::TextDisabled(
          "Kinds of object it places (.object assets), after the region "
          "content's own, each by the cells of its placement size and as far "
          "as it draws. Open one to edit its placement and its model");
      int remove = -1;
      for (size_t i = 0; i < planet.objects.assets.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton("Remove"))
          remove = static_cast<int>(i);
        ImGui::SameLine();
        if (IMComponents::Foldout(
                (planet.objects.assets[i] + "###Object").c_str())) {
          // The asset's own editor: it saves the file and rebuilds the planets
          if (auto asset =
                  Runtime::Project().Assets().Find(planet.objects.assets[i])) {
            ImGui::PushTextWrapPos(-1.0f);
            asset->DrawInspectorUI();
            ImGui::PopTextWrapPos();
          } else {
            ImGui::TextDisabled("Not an asset of the project");
          }
        }
        ImGui::PopID();
      }
      if (remove >= 0) {
        planet.objects.assets.erase(planet.objects.assets.begin() + remove);
        changed = true;
      }
      static char added[260] = "";
      if (ImGui::InputTextWithHint("##AddObject", "objects/rock.object", added,
                                   sizeof(added),
                                   ImGuiInputTextFlags_EnterReturnsTrue) &&
          added[0] != '\0') {
        planet.objects.assets.push_back(added);
        added[0] = '\0';
        changed = true;
      }
      ImGui::TextDisabled("Type a project-relative path and press Enter");
      // TODO: Drop .object assets from the asset browser here
    }

    // Applies from the next frame, no rebuild needed
    if (_BeginStage(planet.herds, "Herds",
                    "Herds come from the cells of this level", 0, 5,
                    "Only over regions this near; 0: as far as the level",
                    false, changed)) {
      ProcWorld::HerdSettings& herd = planet.herds.creatures;
      ImGui::Checkbox("Roam", &herd.roam);
      ImGui::DragFloat("Walk Speed", &herd.walk_speed, 0.05f, 0.0f, 50.0f,
                       "%.2f m/s");
      ImGui::TextDisabled(
          "Every creature's speed, times its model's speed scale");
      ImGui::DragFloat("Turn Rate", &herd.turn_rate, 1.0f, 1.0f, 1080.0f,
                       "%.0f deg/s");
      ImGui::DragFloat("Roam Radius", &herd.roam_radius, 0.5f, 0.0f, 1000.0f,
                       "%.1f m");
      ImGui::TextDisabled("Walks end this near where a creature was placed");
      ImGui::DragFloatRange2("Rest", &herd.rest_min, &herd.rest_max, 0.1f, 0.0f,
                             600.0f, "%.1f s");
      ImGui::DragFloat("Nav Reach", &herd.nav_reach, 0.1f, 0.0f, 100.0f,
                       "%.1f m");
      ImGui::TextDisabled("Points find navmesh polygons this near");
      ImGui::DragFloat("Roam Distance", &herd.roam_distance, 5.0f, 0.0f, 1.0e5f,
                       "%.0f m");
      ImGui::TextDisabled("Only creatures this near the viewer roam");
      char clip[128];
      std::snprintf(clip, sizeof(clip), "%s", herd.walk_clip.c_str());
      if (ImGui::InputText("Walk Clip", clip, sizeof(clip)))
        herd.walk_clip = clip;
      std::snprintf(clip, sizeof(clip), "%s", herd.rest_clip.c_str());
      if (ImGui::InputText("Rest Clip", clip, sizeof(clip)))
        herd.rest_clip = clip;
      ImGui::TextDisabled("The first clip whose name holds this, in any case");
      ImGui::DragFloat("Fade", &herd.fade, 0.01f, 0.0f, 5.0f, "%.2f s");
      ImGui::DragFloat3("Forward", &herd.forward.x, 0.01f, -1.0f, 1.0f);
      ImGui::TextDisabled(
          "The axis the models face along; zero: each walk clip's own");
      ImGui::Checkbox("Gaits", &herd.match_stride);
      ImGui::TextDisabled(
          "The gait nearest the speed, at a rate within its own");
      ImGui::TextDisabled("Off: the first clip at speed 1, at Walk Speed");
      ImGui::DragFloatRange2("Walk Rates", &herd.walk_min_rate,
                             &herd.walk_max_rate, 0.01f, 0.0f, 10.0f, "%.2fx");
      ImGui::TextDisabled("For models without gaits: the walk clip's rates");
      ImGui::Checkbox("Keep Pace", &herd.keep_pace);
      ImGui::TextDisabled(
          "Move as fast as the gait reaches, so feet don't slide");
      ImGui::SeparatorText("Animation");
      InspectableComponents::DrawAnimatorSettings(herd.animation);
    }

    if (_BeginStage(planet.sea, "Sea", nullptr, 0, 0, nullptr, true, changed)) {
      // The surface is made again on regenerating
      Water::SurfaceSettings& water = planet.sea.water;
      ImGui::DragFloat("Patch Size", &water.patch_size, 16.0f, 16.0f, 1.0e5f,
                       "%.0f m");
      changed |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::TextDisabled("Across the grid drawn under the viewer");
      ImGui::SliderInt("Grid Resolution", &water.grid_resolution, 2, 1024);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::SliderInt("Cascades", &water.cascades.count, 1, 16);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::SliderInt("Cascade Resolution", &water.cascades.resolution, 8,
                       1024);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::DragFloat("Base Scale", &water.cascades.base_scale, 0.05f, 0.01f,
                       100.0f);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::TextDisabled("Cascade 0 is base scale x 4 m; each next one twice");
      ImGui::DragFloat("Min Texels", &water.cascades.min_texels, 0.1f, 1.0f,
                       64.0f);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::TextDisabled("Texels across a cascade's shortest wave");
      ImGui::DragFloat("Gravity", &water.cascades.gravity, 0.05f, 0.0f, 100.0f,
                       "%.2f m/s2");
      changed |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::ColorEdit3("Colour", &water.look.colour.x);
      changed |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::SliderFloat("Roughness", &water.look.roughness, 0.0f, 1.0f);
      changed |= ImGui::IsItemDeactivatedAfterEdit();

      ImGui::Checkbox("Force Weather Event", &planet.force_weather_event);
      ImGui::TextDisabled("Picks as though a weather event is under way");
      if (planet.water_state < 0) {
        ImGui::TextDisabled("The planet content has no water states");
      } else {
        ImGui::Text("State %d (from %d, %.0f%%)", planet.water_state,
                    planet.water_state_from, planet.water_blend * 100.0f);
        ImGui::TextDisabled("Weights and timing come from the planet content");
      }
    }
  }

  ImGui::Separator();
  if (!planet.error.empty())
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                       planet.error.c_str());
  else
    ImGui::Text("%zu regions loaded, %zu building, %zu queued",
                planet.loaded_regions, planet.building_regions,
                planet.queued_regions);
  if (planet.planet_terrain)
    ImGui::Text("Terrain from the planet (replaces the content's)");
  if (planet.planet_radius > 0.0f)
    ImGui::Text("Radius %.0f m from the size class", planet.planet_radius);
  if (planet.planet_tile_colours > 0)
    ImGui::Text("%d tile colours from the planet (they replace the look's)",
                planet.planet_tile_colours);

  if (changed)
    _RequestRebuild(id);
}

//
//  PROFILE
//

// Profiles as last loaded or saved, by project-relative path
// TODO: reload when the asset changes on disk
static std::unordered_map<std::string, nlohmann::json> s_profiles;

static const nlohmann::json& _Profile(const std::string& path) {
  auto it = s_profiles.find(path);
  if (it == s_profiles.end())
    it = s_profiles
             .emplace(path,
                      PCG::LoadProfile(Runtime::Project().AbsolutePath(path)))
             .first;
  return it->second;
}

// Marks every volume whose generator starts from the profile for a rebuild
// and gives each its settings: the profile with its own overrides
static void _ApplyProfile(const std::string& path,
                          const nlohmann::json& previous,
                          const nlohmann::json& profile) {
  auto& resources = EngineContext::resourceManager();
  for (auto [entity, volume] : ECS::Main().View<VolumeComponent>().each()) {
    auto resource =
        resources.GetResourceAs<PCG::GeneratorResource>(volume.generator_id);
    if (!resource || !resource->Get() || resource->profile_ != path)
      continue;
    nlohmann::json settings = profile;
    settings.merge_patch(PCG::ProfileOverrides(
        previous, ProcWorld::WriteSettings(*resource->Get())));
    ProcWorld::ReadSettings(*resource->Get(), settings);
    volume.dirty = true;
  }
}

// The profile asset the generator's settings start from and what the
// generator sets over it
static void _DrawProfile(ResourceID id, PCG::GeneratorResource& resource) {
  PCG::Generator& generator = *resource.Get();
  if (!IMComponents::Foldout("Profile", true))
    return;

  if (resource.profile_.empty()) {
    ImGui::TextDisabled("Settings shared with other generators: a .generator "
                        "asset, project-relative");
    static char path[260] = "";
    ImGui::InputTextWithHint("##Profile", "profiles/planet.generator", path,
                             sizeof(path));
    if (ImGui::Button("Use") && path[0] != '\0') {
      const nlohmann::json& profile = _Profile(path);
      if (profile.value("type", std::string()) !=
          PCG::GetGeneratorTypeName(generator.GetType())) {
        s_profiles.erase(path);
        Logger::getInstance().Log(
            LogLevel::Warning,
            std::string("Not a profile for this generator: ") + path);
      } else {
        ProcWorld::ReadSettings(generator, profile);
        resource.profile_ = path;
        _RequestRebuild(id);
        Runtime::Project().MarkSceneDirty();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As") && path[0] != '\0') {
      const nlohmann::json settings = ProcWorld::WriteSettings(generator);
      if (PCG::SaveProfile(Runtime::Project().AbsolutePath(path), settings)) {
        s_profiles[path] = settings;
        resource.profile_ = path;
        Runtime::Project().MarkSceneDirty();
      } else {
        Logger::getInstance().Log(
            LogLevel::Error, std::string("Can't save the profile ") + path);
      }
    }
    ImGui::TextDisabled("Use: take its settings. Save As: write these");
    return;
  }

  ImGui::Text("%s", resource.profile_.c_str());
  const nlohmann::json& profile = _Profile(resource.profile_);
  if (profile.is_null()) {
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                       "Missing: every setting is this generator's own");
  }
  const nlohmann::json overrides =
      PCG::ProfileOverrides(profile, ProcWorld::WriteSettings(generator));
  if (overrides.empty())
    ImGui::TextDisabled("No overrides");
  else
    ImGui::TextDisabled("Overrides, set here over the profile:");
  std::string revert;
  for (const auto& [key, value] : overrides.items()) {
    ImGui::PushID(key.c_str());
    if (ImGui::SmallButton("Revert"))
      revert = key;
    ImGui::SameLine();
    ImGui::TextUnformatted(key.c_str());
    ImGui::PopID();
  }
  if (!revert.empty() && profile.contains(revert)) {
    ProcWorld::ReadSettings(generator, {{revert, profile.at(revert)}});
    _RequestRebuild(id);
    Runtime::Project().MarkSceneDirty();
  }

  if (ImGui::Button("Apply to Profile")) {
    // The other generators keep their own overrides
    const std::string path = resource.profile_;
    const nlohmann::json previous = profile;
    const nlohmann::json settings = ProcWorld::WriteSettings(generator);
    if (PCG::SaveProfile(Runtime::Project().AbsolutePath(path), settings)) {
      s_profiles[path] = settings;
      _ApplyProfile(path, previous, settings);
      Runtime::Project().MarkSceneDirty();
    } else {
      Logger::getInstance().Log(LogLevel::Error,
                                "Can't save the profile " + path);
    }
    return;
  }
  ImGui::SameLine();
  if (ImGui::Button("Revert All") && !profile.is_null()) {
    ProcWorld::ReadSettings(generator, profile);
    _RequestRebuild(id);
    Runtime::Project().MarkSceneDirty();
  }
  ImGui::SameLine();
  if (ImGui::Button("Detach")) {
    resource.profile_.clear();
    Runtime::Project().MarkSceneDirty();
  }
  ImGui::TextDisabled("Apply: these settings become the profile's, for "
                      "every generator using it");
}

void Draw(ResourceID id) {
  auto resource =
      EngineContext::resourceManager().GetResourceAs<PCG::GeneratorResource>(
          id);
  if (!resource)
    return;
  ImGui::PushID(static_cast<int>(id));
  // Room for the labels and hints in the narrow Inspector
  ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
  ImGui::PushTextWrapPos(0.0f);
  if (resource->Get() &&
      !ProcWorld::WriteSettings(*resource->Get()).is_null())
    _DrawProfile(id, *resource);
  switch (resource->GetGeneratorType()) {
    case PCG::GeneratorType::Region:
      _DrawRegion(id);
      break;
    case PCG::GeneratorType::Craft:
      _DrawCraft(id);
      break;
    case PCG::GeneratorType::Planet:
      _DrawPlanet(id);
      break;
    default:
      break;
  }
  ImGui::PopTextWrapPos();
  ImGui::PopItemWidth();
  ImGui::PopID();
}

void Create(VolumeComponent& volume, PCG::GeneratorType type) {
  auto [id, resource] =
      EngineContext::resourceManager().Create<PCG::GeneratorResource>(
          PCG::GetGeneratorTypeName(type));
  switch (type) {
    case PCG::GeneratorType::Region:
      resource->SetGenerator(std::make_unique<ProcWorld::RegionGenerator>());
      break;
    case PCG::GeneratorType::Craft:
      resource->SetGenerator(std::make_unique<ProcWorld::CraftGenerator>());
      break;
    case PCG::GeneratorType::Planet:
      resource->SetGenerator(std::make_unique<ProcWorld::PlanetGenerator>());
      break;
    default:
      resource->SetGenerator(PCG::CreateGenerator(type));
      break;
  }
  volume.generator_id = id;
  volume.dirty = true;
  // Graphs edit in the PCG tab, the rest in the Inspector
  if (type == PCG::GeneratorType::Graph)
    EditorEvents::open_graph_editor(id);
}

Entity AddGenerator(Entity world, PCG::GeneratorType type) {
  auto& ecs = ECS::Main();
  const Entity entity =
      std::get<0>(ecs.CreateEntity(PCG::GetGeneratorTypeName(type), world));
  Create(ecs.Add<VolumeComponent>(entity), type);
  return entity;
}

}  // namespace GeneratorProperties
