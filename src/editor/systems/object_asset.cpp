#include "object_asset.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "content_json.h"
#include "editor/gui/components/im_components.h"
#include "editor/gui/components/inspectable_components.h"
#include "editor/runtime/runtime.h"
#include "engine/context/engine_context.h"
#include "engine/ecs/components/volume_component.h"
#include "engine/pcg/generator_resource.h"
#include "engine/pcg/pcg_engine.h"
#include "engine/pcg/procworld/planet_generator.h"
#include "engine/renderer/icons/icon_loader.h"

void ObjectAsset::OnDefaultLoad(const std::filesystem::path& meta_path) {
  object_ = {};
  error_.clear();
  std::stringstream text;
  text << std::ifstream(Runtime::Project().AbsolutePath(GetPath())).rdbuf();
  if (!Worldgen::Json::LoadRegionObject(text.str(), object_, &error_))
    object_ = {};
}

void ObjectAsset::OnReload() {
  OnDefaultLoad({});
}

void ObjectAsset::Save() {
  std::ofstream(Runtime::Project().AbsolutePath(GetPath()), std::ios::binary)
      << Worldgen::Json::SaveRegionObject(object_);
  RebuildPlanets();
}

void ObjectAsset::RebuildPlanets() const {
  const std::string path = GetPath().generic_string();
  auto& resources = EngineContext::resourceManager();
  for (auto [entity, volume] : ECS::Main().View<VolumeComponent>().each()) {
    auto resource =
        resources.GetResourceAs<PCG::GeneratorResource>(volume.generator_id);
    if (!resource || !resource->Get() ||
        resource->GetGeneratorType() != PCG::GeneratorType::Planet)
      continue;
    const auto& planet =
        static_cast<const ProcWorld::PlanetGenerator&>(*resource->Get());
    if (std::find(planet.objects.begin(), planet.objects.end(), path) !=
        planet.objects.end())
      volume.dirty = true;
  }
}

template <class Enum, size_t N>
static void _Choice(const char* label, Enum& value,
                    const std::array<const char*, N>& names) {
  int index = static_cast<int>(value);
  if (IMComponents::Choice(label, index, names.data(), static_cast<int>(N)))
    value = static_cast<Enum>(index);
}

void ObjectAsset::DrawInspectorUI() {
  using namespace InspectableComponents;
  if (!error_.empty()) {
    ImGui::TextWrapped("Can't read this object: %s", error_.c_str());
    return;
  }
  Worldgen::ObjectSpawn& spawn = object_.spawn;
  const std::string before = Worldgen::Json::SaveRegionObject(object_);

  if (BeginSection("Object")) {
    IMComponents::InputText("Model", object_.id, "procmodel:models/rock.json");
    ImGui::TextDisabled("A model file, or procmodel: and a procedural model");
    // Region sizes are 32 m doubled per level
    std::array<std::string, 9> labels;
    std::array<const char*, 9> names;
    int level = 0;
    for (int i = 0; i < 9; ++i) {
      labels[i] = std::to_string(32 << i) + " m";
      names[i] = labels[i].c_str();
      if (object_.placement_size == 32 << i)
        level = i;
    }
    if (IMComponents::Choice("Placement Size", level, names.data(), 9))
      object_.placement_size = 32 << level;
    ImGui::TextDisabled("Regions this wide place it: small sizes for small");
    ImGui::TextDisabled("objects seen close, large ones for objects seen far");
    _Choice("Ground Tile", object_.tile_type,
            std::array<const char*, 9>{"Air", "Base", "Rock", "Mountain",
                                       "Underwater", "Cave", "Dirt", "Liquid",
                                       "Substance"});
    IMComponents::Input("Allow Limiting", object_.allow_limiting);
    ImGui::TextDisabled("Quality caps may trim it in crowded regions");
    EndSection();
  }

  if (BeginSection("Density")) {
    std::string placement(spawn.placement.bytes,
                          strnlen(spawn.placement.bytes, 16));
    IMComponents::InputText("Spawn Density", placement, "a density's name");
    std::fill(std::begin(spawn.placement.bytes),
              std::end(spawn.placement.bytes), '\0');
    std::copy_n(placement.begin(), std::min<size_t>(placement.size(), 16),
                spawn.placement.bytes);
    IMComponents::Input("Flat Density", spawn.flat_density, 0.001f);
    IMComponents::Input("Slope Density", spawn.slope_density, 0.001f);
    IMComponents::Input("Slope Multiplier", spawn.slope_multiplier, 0.01f);
    IMComponents::Input("Coverage", spawn.coverage, 0.01f);
    IMComponents::Input("Patch Edge Scaling", spawn.patch_edge_scaling, 0.01f);
    _Choice("Near Buildings", spawn.large_object_coverage,
            std::array<const char*, 7>{
                "Do Not Place", "Do Not Place (Ignore Footprint)",
                "Do Not Place Close", "Do Not Place Anywhere Near",
                "Only Place Around", "Only Place Around (Ignore Footprint)",
                "Always Place"});
    _Choice("Overlap", spawn.overlap_style,
            std::array<const char*, 3>{"None", "Same Seed", "All"});
    _Choice("Priority", spawn.placement_priority,
            std::array<const char*, 3>{"Low", "Normal", "High"});
    IMComponents::Input("Order", spawn.order);
    EndSection();
  }

  if (BeginSection("Ground")) {
    Headline("Slope (degrees from up)");
    IMComponents::Input("Min Angle", spawn.min_angle, 0.5f);
    IMComponents::Input("Max Angle", spawn.max_angle, 0.5f);
    Headline("Height (m)");
    IMComponents::Input("Min Height", spawn.min_height, 1.0f);
    IMComponents::Input("Max Height", spawn.max_height, 1.0f);
    IMComponents::Input("From Sea Level", spawn.relative_to_sea_level);
    _Choice("Ground Colour", spawn.ground_colour_index,
            std::array<const char*, 3>{"Auto", "Main", "Patch"});
    EndSection();
  }

  if (BeginSection("Shape")) {
    Headline("Scale");
    IMComponents::Input("Min Scale", spawn.min_scale, 0.01f);
    IMComponents::Input("Max Scale", spawn.max_scale, 0.01f);
    IMComponents::Input("Min Scale Y", spawn.min_scale_y, 0.01f);
    IMComponents::Input("Max Scale Y", spawn.max_scale_y, 0.01f);
    Headline("Rotation (degrees)");
    IMComponents::Input("Max Y Rotation", spawn.max_y_rotation, 1.0f);
    IMComponents::Input("Max XZ Rotation", spawn.max_xz_rotation, 0.5f);
    IMComponents::Input("Align To Normal", spawn.align_to_normal);
    Headline("Height Offset (m)");
    IMComponents::Input("Max Raise", spawn.max_raise, 0.05f);
    IMComponents::Input("Max Lower", spawn.max_lower, 0.05f);
    IMComponents::Input("Auto Collision", spawn.auto_collision);
    EndSection();
  }

  if (BeginSection("Seed")) {
    IMComponents::Input("Own Seed", spawn.seed_valid);
    if (spawn.seed_valid) {
      std::string seed = std::to_string(spawn.seed);
      if (IMComponents::InputText("Seed", seed))
        spawn.seed = std::strtoull(seed.c_str(), nullptr, 10);
    }
    ImGui::TextDisabled("Off: the seed comes from its place among the");
    ImGui::TextDisabled("planet's objects of its size");
    EndSection();
  }

  if (BeginSection("Draw Distance")) {
    IMComponents::Input("Fade Start", spawn.fade_start, 1.0f);
    IMComponents::Input("Fade End", spawn.fade_end, 1.0f);
    ImGui::TextDisabled("Overrides the model's draw distance; End at or");
    ImGui::TextDisabled("below Start: the model's own");
    // TODO: fade_offset and lod_distances (the content's own LOD distances)
    EndSection();
  }

  static constexpr std::string_view prefix = "procmodel:";
  if (object_.id.rfind(prefix, 0) == 0 && BeginSection("Imposter")) {
    if (const std::string descriptor = object_.id.substr(prefix.size());
        descriptor != descriptor_) {
      descriptor_ = descriptor;
      imposter_pending_ = false;
      imposter_ = ImposterSettings::Read(
          Runtime::Project().AbsolutePath(descriptor_));
    }
    const ImposterSettings was = imposter_;
    IMComponents::Input("Distance", imposter_.distance, 1.0f);
    IMComponents::Input("Resolution", imposter_.resolution, 16.0f);
    IMComponents::Input("Views", imposter_.views, 0.1f);
    IMComponents::Input("Variants", imposter_.variants, 0.1f);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled(
        "From the distance on, instances draw as a card turned to the "
        "viewer, baked from Views x Views directions above it (0: never). "
        "Variants: seeds baked side by side, Resolution pixels each; they "
        "blend in over the model's LOD Blend share");
    ImGui::TextDisabled("%s",
                        ("The model's own (" + descriptor_ +
                         "): every object placing it shares them")
                            .c_str());
    ImGui::PopTextWrapPos();
    imposter_pending_ |= imposter_.distance != was.distance ||
                         imposter_.resolution != was.resolution ||
                         imposter_.views != was.views ||
                         imposter_.variants != was.variants;
    EndSection();
  }

  // Saved once an edit ends, so a drag doesn't rebuild planets every frame
  pending_ |= Worldgen::Json::SaveRegionObject(object_) != before;
  if (pending_ && !ImGui::IsAnyItemActive()) {
    pending_ = false;
    Save();
  }
  if (imposter_pending_ && !ImGui::IsAnyItemActive()) {
    imposter_pending_ = false;
    const auto descriptor = Runtime::Project().AbsolutePath(descriptor_);
    ImposterSettings::Write(descriptor, imposter_);
    EngineContext::PCG().GetProcModel().ReloadDrawSettings(descriptor);
    // Planets leave out the instances of models drawn as imposters
    RebuildPlanets();
  }
}

uint32_t ObjectAsset::GetIcon() const {
  return IconLoader::GetIconHandle("file");
}
