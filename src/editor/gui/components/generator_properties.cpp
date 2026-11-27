#include "generator_properties.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "editor/events.h"
#include "editor/gui/components/im_components.h"
#include "editor/gui/components/inspectable_components.h"
#include "editor/gui/components/instance_edits.h"
#include "editor/gui/utils/asset_drag.h"
#include "editor/runtime/runtime.h"
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"
#include "engine/context/engine_context.h"
#include "engine/core/logger.h"
#include "engine/ecs/world.h"
#include "engine/pcg/generator_profile.h"
#include "engine/pcg/generator_resource.h"
#include "engine/pcg/graph/generator_graph.h"
#include "engine/pcg/graph/graph_serializer.h"
#include "engine/pcg/points/spawner_generator.h"
#include "engine/pcg/procworld/generator_settings.h"
#include "engine/pcg/procworld/craft_generator.h"
#include "engine/pcg/procworld/planet_generator.h"
#include "engine/pcg/procworld/region_generator.h"
#include "engine/pcg/procworld/star_system_source.h"
#include "engine/pcg/stage.h"

namespace GeneratorProperties {

// Marks the volumes using the generator for a rebuild, and the scene, which
// saves its settings, changed
static void _RequestRebuild(ResourceID id) {
  for (auto [entity, volume] : ECS::Main().View<VolumeComponent>().each()) {
    if (volume.generator_id == id)
      volume.dirty = true;
  }
  Runtime::Project().MarkSceneDirty();
}

// A path field, project-relative; true once one is entered
static bool _Path(const char* label, std::string& path, const char* tooltip) {
  return IMComponents::InputText(label, path, "", tooltip);
}

static bool _Seed(uint64_t& seed) {
  return IMComponents::Row("Seed", [&] {
    ImGui::InputScalar("##seed", ImGuiDataType_U64, &seed);
    return ImGui::IsItemDeactivatedAfterEdit();
  });
}

// What the last build gave, or its error
static void _Status(const std::string& error, const std::string& status) {
  if (!error.empty())
    IMComponents::Label(error, EditorColor::console_error);
  else
    IMComponents::Label(status, EditorColor::text_dim);
}

// Tile colours and material settings; they apply without a rebuild
static void _DrawTerrainLook(TerrainMaterialData& look) {
  if (!IMComponents::Foldout("Terrain Look"))
    return;
  IMComponents::Slider("Roughness", look.roughness, 0.04f, 1.0f);
  // Which look of a tile shows, from two noise samples
  if (IMComponents::Foldout("Tile Noise")) {
    glm::vec4& noise = look.tile_noise;
    IMComponents::Drag("First Frequency", noise.x, 0.001f, 0.0f, 10.0f,
                       "%.3f", "The first sample's, per metre");
    IMComponents::Drag("Second Frequency", noise.y, 0.001f, 0.0f, 10.0f,
                       "%.3f", "The second sample's, per metre");
    IMComponents::Drag("Second Weight", noise.z, 0.01f, 0.0f, 2.0f, "%.2f",
                       "How much the second sample counts");
    IMComponents::Drag("Offset", noise.w, 0.01f, -1.0f, 1.0f, "%.2f",
                       "Shifts the blend toward one look or the other");
  }
  for (int i = 0; i < TerrainMaterialData::tile_count; ++i) {
    ImGui::PushID(i);
    const std::string title = "Tile " + std::to_string(i) +
                              (look.tile_images[i].empty()
                                   ? std::string()
                                   : "  " + look.tile_images[i]) +
                              "###Tile";
    if (IMComponents::Foldout(title.c_str())) {
      IMComponents::ColorPicker(
          "Colour", look.tile_colours[i],
          "Tiles 0-15: groups of steep (even) and flat (odd)");
      _Path("Image", look.tile_images[i],
            "Relative to the project: RGB colour, alpha height");
      IMComponents::Input("Height Offset", look.height_offset[i], 0.01f);
      IMComponents::Input("Height Scale", look.height_scale[i], 0.01f);
    }
    ImGui::PopID();
  }
}

//
//  REGION PROPERTIES
//
static void _DrawRegion(ResourceID id) {
  auto resource =
      EngineContext::resourceManager().GetResourceAs<PCG::GeneratorResource>(
          id);
  if (!resource || !resource->Get() ||
      resource->GetGeneratorType() != PCG::GeneratorType::Region) {
    IMComponents::Label("The generator no longer exists", EditorColor::text_dim);
    return;
  }
  auto& region = static_cast<ProcWorld::RegionGenerator&>(*resource->Get());

  bool changed = false;
  changed |= _Path("Content", region.content_path,
                   "JSON region content, relative to the project");
  changed |= IMComponents::Row(
      "Cell",
      [&] {
        return ImGui::InputInt3("##cell", &region.cell.x,
                                ImGuiInputTextFlags_EnterReturnsTrue);
      },
      "Planet-space cell centre; zero: above the north pole");
  changed |= _Path("Planet", region.planet_path,
                   "JSON planet content, optional: its buildings");
  changed |= _Seed(region.seed);

  changed |= ImGui::Button("Regenerate");

  _DrawTerrainLook(region.look);

  _Status(region.error, std::to_string(region.vertex_count) + " vertices, " +
                            std::to_string(region.instance_count) +
                            " objects");

  if (changed)
    _RequestRebuild(id);
}

//
//  CRAFT PROPERTIES
//
static void _DrawCraft(ResourceID id) {
  auto resource =
      EngineContext::resourceManager().GetResourceAs<PCG::GeneratorResource>(
          id);
  if (!resource || !resource->Get() ||
      resource->GetGeneratorType() != PCG::GeneratorType::Craft) {
    IMComponents::Label("The generator no longer exists", EditorColor::text_dim);
    return;
  }
  auto& craft = static_cast<ProcWorld::CraftGenerator&>(*resource->Get());

  bool changed = false;
  changed |= _Path("Content", craft.content_path,
                   "JSON craft content, relative to the project");
  changed |= _Seed(craft.seed);
  changed |= IMComponents::Input(
      "Lawless", craft.lawless,
      "Some craft change class at the content's chances");
  changed |= IMComponents::Input(
      "Legacy Colours", craft.legacy_colours,
      "Palettes drawn the older way, as old saves keep");

  changed |= ImGui::Button("Regenerate");

  _Status(craft.error, std::to_string(craft.craft_count) + " craft, " +
                           std::to_string(craft.missing_count) + " missing");

  if (changed)
    _RequestRebuild(id);
}

//
//  PLANET PROPERTIES
//

// A stage's settings: its common ones, then (while this returns true) its
// own. Only the stage `only` names draws; with `titled`, in a foldout
// titled by its name with its on/off checkbox (otherwise its section has
// them). `grid_tooltip` and `radius_tooltip` say what the grid and the
// radius mean for it (null: it has none). With `rebuild`, editing them
// regenerates the planet; turning it off never does (it keeps what it made,
// hidden)
static bool _BeginStage(PCG::Stage& stage, const char* id,
                        const std::string& only, bool titled,
                        const char* grid_tooltip, int grid_min, int grid_max,
                        const char* radius_tooltip, bool rebuild,
                        bool& changed) {
  if (only != id)
    return false;
  if (titled) {
    const std::string title =
        (stage.name.empty() ? std::string("Unnamed") : stage.name) + "###" +
        id;
    if (!IMComponents::Foldout(title.c_str(), &stage.enabled))
      return false;
  }
  ImGui::PushID(id);
  bool edited = false;
  IMComponents::InputText("Name", stage.name, "",
                          "Shown in the stage list and in its entities' path");
  if (grid_tooltip)
    edited |= IMComponents::Slider("Grid", stage.grid, grid_min, grid_max,
                                   grid_tooltip);
  if (radius_tooltip)
    edited |= IMComponents::Drag("Generation Radius", stage.generation_radius,
                                 16.0f, 0.0f, 1.0e6f, "%.0f m", radius_tooltip);
  ImGui::PopID();
  if (rebuild)
    changed |= edited;
  return true;
}

// The planet's stage `only` names (its name in the settings); `titled`: in
// a foldout
static void _DrawStage(ProcWorld::PlanetGenerator& planet,
                       const std::string& only, bool titled, bool& changed) {
  // Loading settings apply from the next frame, no rebuild needed
  ProcWorld::TerrainStage& terrain = planet.terrain;
  if (_BeginStage(terrain, "terrain", only, titled, "The finest level", 0, 5,
                  "Regions farther are not loaded; 0: no limit", false,
                  changed)) {
    IMComponents::Slider("Coarsest Level", terrain.coarsest_level,
                         terrain.grid, 9);
    IMComponents::Slider("Split Distance", terrain.split_distance, 0.5f, 8.0f,
                         "%.2f",
                         "A region splits when nearer than this many widths");
  }

  _BeginStage(planet.buildings, "buildings", only, titled,
              "Placed on the terrain's regions of this level or finer", 0, 9,
              "Only on regions this near; 0: as far as the terrain", true,
              changed);

  if (_BeginStage(planet.objects, "objects", only, titled, nullptr, 0, 0,
                  "No kind places farther; 0: each as far as it draws", false,
                  changed)) {
    // Each kind's own editor: it saves the file and rebuilds the planets
    int remove = -1;
    for (size_t i = 0; i < planet.objects.assets.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      if (ImGui::SmallButton("Remove"))
        remove = static_cast<int>(i);
      ImGui::SameLine();
      if (IMComponents::Foldout(
              (planet.objects.assets[i] + "###Object").c_str())) {
        if (auto asset =
                Runtime::Project().Assets().Find(planet.objects.assets[i]))
          asset->DrawInspectorUI();
        else
          IMComponents::Label("Not an asset of the project",
                              EditorColor::text_dim);
      }
      ImGui::PopID();
    }
    if (remove >= 0) {
      planet.objects.assets.erase(planet.objects.assets.begin() + remove);
      changed = true;
    }
    std::string added;
    if (IMComponents::InputText(
            "Add", added, "objects/rock.object",
            "A kind of object to place (.object asset, project-relative), "
            "after the region content's own: by the cells of its placement "
            "size, as far as it draws") &&
        !added.empty()) {
      planet.objects.assets.push_back(added);
      changed = true;
    }
    // Or dropped from the asset browser
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* payload = AssetDrag::Accept({".object"})) {
        planet.objects.assets.push_back(AssetDrag::Path(*payload));
        changed = true;
      }
      ImGui::EndDragDropTarget();
    }
  }

  // Applies from the next frame, no rebuild needed
  if (_BeginStage(planet.herds, "herds", only, titled,
                  "Herds come from the cells of this level", 0, 5,
                  "Only over regions this near; 0: as far as the level",
                  false, changed)) {
    ProcWorld::HerdSettings& herd = planet.herds.creatures;
    IMComponents::Input("Roam", herd.roam);
    IMComponents::Drag("Walk Speed", herd.walk_speed, 0.05f, 0.0f, 50.0f,
                       "%.2f m/s",
                       "Every creature's speed, times its model's speed "
                       "scale");
    IMComponents::Drag("Turn Rate", herd.turn_rate, 1.0f, 1.0f, 1080.0f,
                       "%.0f deg/s");
    IMComponents::Drag("Roam Radius", herd.roam_radius, 0.5f, 0.0f, 1000.0f,
                       "%.1f m",
                       "Walks end this near where a creature was placed");
    IMComponents::DragRange("Rest", herd.rest_min, herd.rest_max, 0.1f, 0.0f,
                            600.0f, "%.1f s");
    IMComponents::Drag("Nav Reach", herd.nav_reach, 0.1f, 0.0f, 100.0f,
                       "%.1f m", "Points find navmesh polygons this near");
    IMComponents::Drag("Roam Distance", herd.roam_distance, 5.0f, 0.0f,
                       1.0e5f, "%.0f m",
                       "Only creatures this near the viewer roam");
    const char* clip_tooltip =
        "The first clip whose name holds this, in any case";
    IMComponents::InputText("Walk Clip", herd.walk_clip, "", clip_tooltip);
    IMComponents::InputText("Rest Clip", herd.rest_clip, "", clip_tooltip);
    IMComponents::Drag("Fade", herd.fade, 0.01f, 0.0f, 5.0f, "%.2f s");
    IMComponents::Input("Forward", herd.forward, 0.01f,
                        "The axis the models face along; zero: each walk "
                        "clip's own");
    IMComponents::Input("Gaits", herd.match_stride,
                        "The gait nearest the speed, at a rate within its "
                        "own. Off: the first clip at speed 1, at Walk Speed");
    IMComponents::DragRange("Walk Rates", herd.walk_min_rate,
                            herd.walk_max_rate, 0.01f, 0.0f, 10.0f, "%.2fx",
                            "For models without gaits: the walk clip's rates");
    IMComponents::Input("Keep Pace", herd.keep_pace,
                        "Move as fast as the gait reaches, so feet don't "
                        "slide");
    if (IMComponents::Foldout("Animation"))
      InspectableComponents::DrawAnimatorSettings(herd.animation);
  }

  if (_BeginStage(planet.sea, "sea", only, titled, nullptr, 0, 0, nullptr,
                  true, changed)) {
    // The surface is made again on regenerating
    Water::SurfaceSettings& water = planet.sea.water;
    changed |= IMComponents::Drag("Patch Size", water.patch_size, 16.0f,
                                  16.0f, 1.0e5f, "%.0f m",
                                  "Across the grid drawn under the viewer");
    changed |= IMComponents::Slider("Grid Resolution", water.grid_resolution,
                                    2, 1024);
    changed |= IMComponents::Slider("Cascades", water.cascades.count, 1, 16);
    changed |= IMComponents::Slider("Cascade Resolution",
                                    water.cascades.resolution, 8, 1024);
    changed |= IMComponents::Drag(
        "Base Scale", water.cascades.base_scale, 0.05f, 0.01f, 100.0f, "%.2f",
        "Cascade 0 is base scale x 4 m; each next one twice");
    changed |= IMComponents::Drag("Min Texels", water.cascades.min_texels,
                                  0.1f, 1.0f, 64.0f, "%.2f",
                                  "Texels across a cascade's shortest wave");
    changed |= IMComponents::Drag("Gravity", water.cascades.gravity, 0.05f,
                                  0.0f, 100.0f, "%.2f m/s2");
    changed |= IMComponents::ColorPicker("Colour", water.look.colour);
    changed |= IMComponents::Row("Roughness", [&] {
      ImGui::SliderFloat("##roughness", &water.look.roughness, 0.0f, 1.0f);
      return ImGui::IsItemDeactivatedAfterEdit();
    });

    IMComponents::Input("Force Weather Event", planet.force_weather_event,
                        "Picks as though a weather event is under way");
    if (planet.water_state < 0)
      IMComponents::Label("The planet content has no water states",
                          EditorColor::text_dim);
    else
      IMComponents::KeyValue(
          "State", std::to_string(planet.water_state) + " (from " +
                       std::to_string(planet.water_state_from) + ", " +
                       std::to_string(static_cast<int>(
                           planet.water_blend * 100.0f)) +
                       "%)");
  }
}

// The planet's stages, grouped by type
static void _DrawStages(ProcWorld::PlanetGenerator& planet, bool& changed) {
  static constexpr const char* ids[] = {"terrain", "buildings", "objects",
                                        "herds", "sea"};
  ImGui::Indent(12.0f);
  for (const PCG::StageType type :
       {PCG::StageType::Surface, PCG::StageType::InstanceSpawner,
        PCG::StageType::EntitySpawner}) {
    const std::string group = std::string(PCG::StageTypeName(type)) + "s";
    if (!IMComponents::Foldout(group.c_str(), true))
      continue;
    ImGui::Indent(12.0f);
    for (const char* id : ids)
      if (planet.GetStage(id)->type == type)
        _DrawStage(planet, id, true, changed);
    ImGui::Unindent(12.0f);
  }
  ImGui::Unindent(12.0f);
}

static void _DrawPlanet(ResourceID id) {
  auto resource =
      EngineContext::resourceManager().GetResourceAs<PCG::GeneratorResource>(
          id);
  if (!resource || !resource->Get() ||
      resource->GetGeneratorType() != PCG::GeneratorType::Planet) {
    IMComponents::Label("The generator no longer exists", EditorColor::text_dim);
    return;
  }
  auto& planet = static_cast<ProcWorld::PlanetGenerator&>(*resource->Get());

  bool changed = false;
  changed |= _Path("Content", planet.content_path,
                   "JSON region content, relative to the project");
  changed |= _Path("Planet", planet.planet_path,
                   "JSON planet content, optional: terrain, radius, colours");
  changed |= _Seed(planet.seed);
  changed |= IMComponents::Row("Biome", [&] {
    ImGui::InputInt("##biome", &planet.biome);
    return ImGui::IsItemDeactivatedAfterEdit();
  });
  changed |= IMComponents::Row("Subtype", [&] {
    ImGui::InputInt("##subtype", &planet.subtype);
    return ImGui::IsItemDeactivatedAfterEdit();
  });
  changed |= IMComponents::Row("Size Class", [&] {
    ImGui::InputInt("##size", &planet.size);
    return ImGui::IsItemDeactivatedAfterEdit();
  });

  IMComponents::Slider("Worker Jobs", planet.worker_jobs, 0, 32,
                       "Regions building at once; 0: one per core but one");
  IMComponents::Drag("Fade Time", planet.fade_time, 0.01f, 0.0f, 10.0f,
                     "%.2f s", "Regions' content fades in and out; 0: at once");

  changed |= ImGui::Button("Regenerate");

  _DrawTerrainLook(planet.look);

  if (IMComponents::Foldout("Weather")) {
    if (!planet.weather_events)
      IMComponents::Label("The planet has no weather events",
                          EditorColor::text_dim);
    else if (!planet.weather_event)
      IMComponents::KeyValue(
          "Clear", "next event in " +
                       std::to_string(static_cast<int>(
                           planet.weather_event_change)) +
                       " s");
    else
      IMComponents::KeyValue(
          planet.weather_event_warning ? "Warning" : "Event",
          "kind " + std::to_string(planet.weather_event_kind) +
              ", intensity " +
              std::to_string(
                  static_cast<int>(planet.weather_event_intensity * 100.0f)) +
              "%, strength " +
              std::to_string(
                  static_cast<int>(planet.weather_event_strength * 100.0f)) +
              "%, next stage in " +
              std::to_string(static_cast<int>(planet.weather_event_change)) +
              " s");
  }

  if (IMComponents::Foldout("Stages", true))
    _DrawStages(planet, changed);

  InstanceEdits::DrawLayer(*resource, planet.orphan_edits);

  std::string status = std::to_string(planet.loaded_regions) +
                       " regions loaded, " +
                       std::to_string(planet.building_regions) +
                       " building, " + std::to_string(planet.queued_regions) +
                       " queued";
  if (planet.planet_terrain)
    status += "; terrain from the planet";
  if (planet.planet_radius > 0.0f)
    status += "; radius " +
              std::to_string(static_cast<int>(planet.planet_radius)) +
              " m from the size class";
  if (planet.planet_tile_colours > 0)
    status += "; " + std::to_string(planet.planet_tile_colours) +
              " tile colours from the planet";
  _Status(planet.error, status);

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
    static char path[260] = "";
    IMComponents::Row(
        "Profile",
        [&] {
          ImGui::InputTextWithHint("##profile", "profiles/planet.generator",
                                   path, sizeof(path));
          return false;
        },
        "Settings shared with other generators: a .generator asset, "
        "project-relative");
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
    IMComponents::Tooltip("Take its settings");
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
    IMComponents::Tooltip("Write these settings to it");
    return;
  }

  IMComponents::KeyValue("Profile", resource.profile_);
  const nlohmann::json& profile = _Profile(resource.profile_);
  if (profile.is_null())
    IMComponents::Label("Missing: every setting is this generator's own",
                        EditorColor::console_error);
  const nlohmann::json overrides =
      PCG::ProfileOverrides(profile, ProcWorld::WriteSettings(generator));
  IMComponents::Label(overrides.empty() ? "No overrides" : "Overrides",
                      EditorColor::text_dim);
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
  IMComponents::Tooltip(
      "These settings become the profile's, for every generator using it");
  ImGui::SameLine();
  if (ImGui::Button("Revert All") && !profile.is_null()) {
    ProcWorld::ReadSettings(generator, profile);
    _RequestRebuild(id);
  }
  IMComponents::Tooltip("Drop every override");
  ImGui::SameLine();
  if (ImGui::Button("Detach")) {
    resource.profile_.clear();
    Runtime::Project().MarkSceneDirty();
  }
  IMComponents::Tooltip("Keep these settings as the generator's own");
}

void DrawStage(ResourceID id, const std::string& stage) {
  auto resource =
      EngineContext::resourceManager().GetResourceAs<PCG::GeneratorResource>(
          id);
  if (!resource || !resource->Get() ||
      resource->GetGeneratorType() != PCG::GeneratorType::Planet)
    return;
  bool changed = false;
  ImGui::PushID(static_cast<int>(id));
  ImGui::PushTextWrapPos(0.0f);
  _DrawStage(static_cast<ProcWorld::PlanetGenerator&>(*resource->Get()),
             stage, false, changed);
  ImGui::PopTextWrapPos();
  ImGui::PopID();
  if (changed)
    _RequestRebuild(id);
}

//
//  SPAWNER PROPERTIES
//


static void _DrawSource(PCG::SpawnerGenerator& spawner, bool& changed) {
  static const char* const types[] = {"Grid", "StarSystem"};
  static const char* const names[] = {"Grid", "Star System"};
  int type = spawner.source->Type() == "StarSystem" ? 1 : 0;
  if (IMComponents::Choice("Source", type, names, 2,
                           "Where the points come from")) {
    spawner.source = ProcWorld::CreatePointSource({{"type", types[type]}});
    // A system's points carry a planet's inputs: bound to a planet
    // template's settings of the same names unless set already
    if (type == 1 && spawner.bindings.empty())
      for (const char* input : {"seed", "biome", "subtype", "size"})
        spawner.bindings.push_back({input, std::string("/") + input});
    changed = true;
  }
  if (auto* grid = dynamic_cast<PCG::GridSource*>(spawner.source.get())) {
    changed |= IMComponents::Row(
        "Count",
        [&] {
          return ImGui::InputInt2("##count", &grid->count.x,
                                  ImGuiInputTextFlags_EnterReturnsTrue);
        },
        "Points across x and z");
    changed |= IMComponents::Drag("Spacing", grid->spacing, 1.0f, 0.0f,
                                  100000.0f, "%.1f", "Metres between points");
    changed |= _Seed(grid->seed);
  } else if (auto* system = dynamic_cast<ProcWorld::StarSystemSource*>(
                 spawner.source.get())) {
    changed |= _Path("Content", system->content_path,
                     "JSON system content, relative to the project");
    Worldgen::SystemAddress& address = system->address;
    changed |= IMComponents::Row(
        "Region",
        [&] {
          return ImGui::InputInt3("##region", &address.x,
                                  ImGuiInputTextFlags_EnterReturnsTrue);
        },
        "The galaxy region: x and z in -2047..2047, y in -127..127");
    changed |= IMComponents::Row("Galaxy", [&] {
      return ImGui::InputInt("##galaxy", &address.galaxy, 1, 1,
                             ImGuiInputTextFlags_EnterReturnsTrue);
    });
    changed |= IMComponents::Row(
        "System",
        [&] {
          return ImGui::InputInt("##system", &address.index, 1, 1,
                                 ImGuiInputTextFlags_EnterReturnsTrue);
        },
        "The system's index in its region, from 1");
  }
}

static void _DrawBindings(PCG::SpawnerGenerator& spawner, bool& changed) {
  if (!IMComponents::Foldout("Bindings", true))
    return;
  int remove = -1;
  for (size_t i = 0; i < spawner.bindings.size(); ++i) {
    PCG::SpawnerGenerator::Binding& binding = spawner.bindings[i];
    ImGui::PushID(static_cast<int>(i));
    changed |= IMComponents::InputText("Attribute", binding.attribute, "seed",
                                       "The point's attribute");
    changed |= IMComponents::InputText(
        "Setting", binding.setting, "/seed",
        "The setting it sets, a JSON pointer into the template's settings");
    std::string table = binding.table.is_null() ? "" : binding.table.dump();
    if (IMComponents::InputText(
            "Table", table, "{\"0\": \"worldgen/lush.json\"}",
            "Optional: the setting's value for each of the attribute's "
            "(JSON object); one it doesn't list keeps the template's")) {
      const nlohmann::json parsed =
          nlohmann::json::parse(table, nullptr, false);
      if (table.empty() || parsed.is_object()) {
        binding.table = table.empty() ? nlohmann::json() : parsed;
        changed = true;
      }
    }
    if (ImGui::SmallButton("Remove"))
      remove = static_cast<int>(i);
    ImGui::Separator();
    ImGui::PopID();
  }
  if (remove >= 0) {
    spawner.bindings.erase(spawner.bindings.begin() + remove);
    changed = true;
  }
  if (ImGui::SmallButton("Add Binding")) {
    spawner.bindings.emplace_back();
    changed = true;
  }
}

static void _DrawPoints(ResourceID id, const PCG::SpawnerGenerator& spawner) {
  if (!IMComponents::Foldout("Points") || spawner.points.empty())
    return;
  // The spawner's volume: its spawned generators are its children
  auto& ecs = ECS::Main();
  Entity volume = entt::null;
  for (auto [entity, component] : ecs.View<VolumeComponent>().each())
    if (component.generator_id == id)
      volume = entity;
  constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_BordersInnerV |
                                    ImGuiTableFlags_SizingStretchProp;
  if (!ImGui::BeginTable("##points", 2, flags))
    return;
  ImGui::TableSetupColumn("Point", ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableSetupColumn("Attributes");
  ImGui::TableHeadersRow();
  for (const PCG::Point& point : spawner.points) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    // Selects what was spawned there
    if (ImGui::Selectable(point.name.c_str()) && volume != entt::null)
      for (const Entity child : ecs.Get<TransformComponent>(volume).children_)
        if (ecs.Get<TransformComponent>(child).name_ == point.name)
          Runtime::State().SelectEntity(child);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(point.attributes.dump().c_str());
  }
  ImGui::EndTable();
}

static void _DrawSpawner(ResourceID id) {
  auto resource =
      EngineContext::resourceManager().GetResourceAs<PCG::GeneratorResource>(
          id);
  if (!resource || !resource->Get() ||
      resource->GetGeneratorType() != PCG::GeneratorType::Spawner) {
    IMComponents::Label("The generator no longer exists", EditorColor::text_dim);
    return;
  }
  auto& spawner = static_cast<PCG::SpawnerGenerator&>(*resource->Get());

  bool changed = false;
  _DrawSource(spawner, changed);
  static const IMComponents::AssetKind profile = {
      "Generator", ICON_FA_DIAGRAM_PROJECT, {".generator"}};
  changed |= IMComponents::AssetField(
      "Template", spawner.template_profile, profile,
      [] { return Runtime::Project().Files(profile.extensions); },
      "The generator profile each point's generator starts from; drop a "
      ".generator from the asset browser, or pick one");
  changed |= _Path("Edits Folder", spawner.edits_folder,
                   "Where each spawned generator keeps its hand edits, one "
                   "file per point (project-relative); empty: "
                   "edits/<this volume's name>");
  _DrawBindings(spawner, changed);
  // Applied to the spawned generators as they are, without a respawn
  bool activation = IMComponents::Drag(
      "Active Within", spawner.activation_distance, 10.0f, 0.0f, 1.0e7f,
      "%.0f m",
      "A spawned generator works fully (a planet streams) only with a "
      "viewer this near its surface, else it shows a stand-in; 0: always");
  int max_active = static_cast<int>(spawner.max_active);
  activation |= IMComponents::Row(
      "Max Active",
      [&] {
        return ImGui::InputInt("##max_active", &max_active, 1, 1,
                               ImGuiInputTextFlags_EnterReturnsTrue);
      },
      "At most this many active at once, the nearest; 0: no cap");
  spawner.max_active = static_cast<uint32_t>(std::max(max_active, 0));
  if (activation)
    Runtime::Project().MarkSceneDirty();
  changed |= ImGui::Button("Regenerate");

  char timing[32];
  std::snprintf(timing, sizeof(timing), "%.1f", spawner.build_ms);
  _Status(spawner.error, std::to_string(spawner.points.size()) + " points, " +
                             std::to_string(spawner.spawned) + " spawned (" +
                             timing + " ms)");
  _DrawPoints(id, spawner);

  if (changed)
    _RequestRebuild(id);
}

static void _DrawGraph(ResourceID id) {
  auto resource =
      EngineContext::resourceManager().GetResourceAs<PCG::GeneratorResource>(
          id);
  if (!resource || !resource->Get() ||
      resource->GetGeneratorType() != PCG::GeneratorType::Graph) {
    IMComponents::Label("The generator no longer exists", EditorColor::text_dim);
    return;
  }
  auto& generator = static_cast<PCG::GeneratorGraph&>(*resource->Get());

  static const IMComponents::AssetKind graph = {
      "PCG Graph", ICON_FA_DIAGRAM_PROJECT, {".pcggraph"}};
  if (IMComponents::AssetField(
          "Graph", generator.graph_path, graph,
          [] { return Runtime::Project().Files(graph.extensions); },
          "The PCG graph it runs; drop a .pcggraph from the asset browser, "
          "or pick one")) {
    if (!generator.graph_path.empty() &&
        !generator.Load(
            Runtime::Project().AbsolutePath(generator.graph_path).string()))
      Logger::getInstance().Log(
          LogLevel::Error, "Can't read the graph " + generator.graph_path);
    _RequestRebuild(id);
  }
  ImGui::BeginDisabled(generator.graph_path.empty());
  if (ImGui::Button(ICON_FA_PEN_TO_SQUARE " Edit"))
    EditorEvents::open_pcg_graph(generator.graph_path);
  ImGui::EndDisabled();
  IMComponents::Tooltip("Open the graph in the PCG Graph tab");
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_PLUS " New")) {
    // graphs/graph.pcggraph, else the first free graphs/graph_<n>.pcggraph
    std::string path = "graphs/graph.pcggraph";
    for (int n = 2;
         std::filesystem::exists(Runtime::Project().AbsolutePath(path)); ++n)
      path = "graphs/graph_" + std::to_string(n) + ".pcggraph";
    const std::string file = Runtime::Project().AbsolutePath(path).string();
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(file).parent_path(), ec);
    if (PCG::GraphSerializer::SaveToFile(PCG::ProgramGraph(), file)) {
      generator.graph_path = path;
      generator.Load(file);
      _RequestRebuild(id);
      EditorEvents::open_pcg_graph(path);
    } else {
      Logger::getInstance().Log(LogLevel::Error, "Can't write " + path);
    }
  }
  IMComponents::Tooltip("An empty graph asset for this generator, in graphs/");
  if (!generator.graph_path.empty() && !generator.IsCompiled())
    IMComponents::Label("The graph doesn't compile", EditorColor::text_dim);
}

void ReloadGraph(const std::string& graph) {
  for (auto [entity, volume] : ECS::Main().View<VolumeComponent>().each()) {
    auto resource =
        EngineContext::resourceManager().GetResourceAs<PCG::GeneratorResource>(
            volume.generator_id);
    if (!resource || !resource->Get() ||
        resource->GetGeneratorType() != PCG::GeneratorType::Graph)
      continue;
    auto& generator = static_cast<PCG::GeneratorGraph&>(*resource->Get());
    if (generator.graph_path != graph)
      continue;
    generator.Load(Runtime::Project().AbsolutePath(graph).string());
    volume.dirty = true;
  }
}

void Draw(ResourceID id) {
  auto resource =
      EngineContext::resourceManager().GetResourceAs<PCG::GeneratorResource>(
          id);
  if (!resource)
    return;
  ImGui::PushID(static_cast<int>(id));
  // Long texts wrap in the narrow Inspector
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
    case PCG::GeneratorType::Spawner:
      _DrawSpawner(id);
      break;
    case PCG::GeneratorType::Graph:
      _DrawGraph(id);
      break;
    default:
      break;
  }
  ImGui::PopTextWrapPos();
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
    case PCG::GeneratorType::Spawner:
      resource->SetGenerator(std::make_unique<PCG::SpawnerGenerator>());
      break;
    default:
      resource->SetGenerator(PCG::CreateGenerator(type));
      break;
  }
  volume.generator_id = id;
  volume.dirty = true;
}

Entity AddGenerator(Entity world, PCG::GeneratorType type) {
  auto& ecs = ECS::Main();
  const Entity entity =
      std::get<0>(ecs.CreateEntity(PCG::GetGeneratorTypeName(type), world));
  Create(ecs.Add<VolumeComponent>(entity), type);
  return entity;
}

}  // namespace GeneratorProperties
