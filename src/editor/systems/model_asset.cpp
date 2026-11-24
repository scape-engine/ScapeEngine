#include "model_asset.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>

#include "editor/gui/components/im_components.h"
#include "editor/gui/components/inspectable_components.h"
#include "editor/runtime/runtime.h"
#include "engine/renderer/icons/icon_loader.h"
#include "engine/scene/model_entity_factory.h"

void ModelAsset::OnDefaultLoad(const std::filesystem::path& meta_path) {
  draw_ = DrawSettings::Read(Runtime::Project().AbsolutePath(GetPath()));
}

void ModelAsset::OnReload() {
  OnDefaultLoad({});
}

void ModelAsset::Apply() {
  DrawSettings::Write(Runtime::Project().AbsolutePath(GetPath()), draw_);
  ModelEntityFactory::ReloadDrawSettings(
      GetPath().generic_string(), Runtime::Project().GetProject().path_);
}

void ModelAsset::DrawInspectorUI() {
  using namespace InspectableComponents;
  const DrawSettings before = draw_;
  bool changed = false;

  if (BeginSection("Draw Distance")) {
    Headline("Fade");
    IMComponents::Input("Fade Start", draw_.fade_start, 1.0f);
    IMComponents::Input("Fade End", draw_.fade_end, 1.0f);
    ImGui::TextDisabled("Dithers out between these distances (m) from the");
    ImGui::TextDisabled("viewer; End at or below Start: always drawn");
    EndSection();
  }

  if (BeginSection("LODs")) {
    ImGui::TextDisabled("From each distance on, the model's meshes draw as");
    ImGui::TextDisabled("the LOD model's (mesh by mesh, nearest first)");
    sources_.resize(draw_.lods.size());
    int remove = -1;
    for (size_t i = 0; i < draw_.lods.size(); ++i) {
      DrawSettings::Lod& lod = draw_.lods[i];
      ImGui::PushID(static_cast<int>(i));
      Headline("LOD " + std::to_string(i + 1));
      IMComponents::Input("Distance", lod.distance, 1.0f);
      std::array<char, 260>& source = sources_[i];
      // Shows the saved path unless it is being typed
      if (ImGui::GetActiveID() != ImGui::GetID("##model") &&
          lod.source != source.data())
        std::snprintf(source.data(), source.size(), "%s", lod.source.c_str());
      ImGui::TextUnformatted("Model:");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(-1.0f);
      ImGui::InputTextWithHint("##model", "models/rock_lod1.glb", source.data(),
                               source.size());
      if (ImGui::IsItemDeactivatedAfterEdit())
        lod.source = source.data();
      if (ImGui::SmallButton("Remove"))
        remove = static_cast<int>(i);
      ImGui::PopID();
    }
    if (remove >= 0) {
      draw_.lods.erase(draw_.lods.begin() + remove);
      sources_.erase(sources_.begin() + remove);
    }
    ImGui::Dummy(ImVec2(0.0f, 3.0f));
    IMComponents::BeginCentered("AddLod");
    if (IMComponents::ButtonBig("Add LOD")) {
      const float last =
          draw_.lods.empty() ? 0.0f : draw_.lods.back().distance;
      draw_.lods.push_back({last + 50.0f, std::string()});
    }
    IMComponents::EndCentered();
    // TODO: generate LOD meshes (simplified copies) instead of naming files
    EndSection();
  }

  changed = draw_.fade_start != before.fade_start ||
            draw_.fade_end != before.fade_end ||
            draw_.lods.size() != before.lods.size();
  for (size_t i = 0; !changed && i < draw_.lods.size(); ++i)
    changed = draw_.lods[i].distance != before.lods[i].distance ||
              draw_.lods[i].source != before.lods[i].source;
  if (changed)
    Apply();
}

uint32_t ModelAsset::GetIcon() const {
  return IconLoader::GetIconHandle("file");
}
