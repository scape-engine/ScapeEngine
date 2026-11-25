#include "model_asset.h"

#include <imgui.h>

#include "editor/gui/components/im_components.h"
#include "editor/gui/components/inspectable_components.h"
#include "editor/runtime/runtime.h"
#include "engine/context/engine_context.h"
#include "engine/pcg/pcg_engine.h"
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
  EngineContext::PCG().GetProcModel().ReloadDrawSettings(
      Runtime::Project().AbsolutePath(GetPath()));
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
    ImGui::TextDisabled("the LOD model's (mesh by mesh, nearest first), or");
    ImGui::TextDisabled("without one as simplified copies of its own");
    float blend = draw_.lod_fade * 100.0f;
    if (IMComponents::Slider("Blend", blend, 0.0f, 50.0f, "%.0f %%"))
      draw_.lod_fade = blend / 100.0f;
    ImGui::TextDisabled("Each LOD dithers in over this share of its");
    ImGui::TextDisabled("distance before it (0: they switch at once)");
    int remove = -1;
    for (size_t i = 0; i < draw_.lods.size(); ++i) {
      DrawSettings::Lod& lod = draw_.lods[i];
      ImGui::PushID(static_cast<int>(i));
      Headline("LOD " + std::to_string(i + 1));
      IMComponents::Input("Distance", lod.distance, 1.0f);
      IMComponents::InputText("Model", lod.source, "generated");
      if (lod.source.empty()) {
        float percent = lod.detail * 100.0f;
        if (IMComponents::Slider("Triangles", percent, 1.0f, 100.0f,
                                 "%.0f %%"))
          lod.detail = percent / 100.0f;
      }
      if (ImGui::SmallButton("Remove"))
        remove = static_cast<int>(i);
      ImGui::PopID();
    }
    if (remove >= 0)
      draw_.lods.erase(draw_.lods.begin() + remove);
    ImGui::Dummy(ImVec2(0.0f, 3.0f));
    IMComponents::BeginCentered("AddLod");
    if (IMComponents::ButtonBig("Add LOD")) {
      const float last =
          draw_.lods.empty() ? 0.0f : draw_.lods.back().distance;
      // Each further LOD half the triangles of the one before
      const float detail = draw_.lods.empty() ? 1.0f : draw_.lods.back().detail;
      draw_.lods.push_back({last + 50.0f, std::string(), detail * 0.5f});
    }
    IMComponents::EndCentered();
    EndSection();
  }

  changed = draw_.fade_start != before.fade_start ||
            draw_.fade_end != before.fade_end ||
            draw_.lod_fade != before.lod_fade ||
            draw_.lods.size() != before.lods.size();
  for (size_t i = 0; !changed && i < draw_.lods.size(); ++i)
    changed = draw_.lods[i].distance != before.lods[i].distance ||
              draw_.lods[i].source != before.lods[i].source ||
              draw_.lods[i].detail != before.lods[i].detail;
  // Applied once an edit ends: each generated detail makes its meshes once
  pending_ |= changed;
  if (pending_ && !ImGui::IsAnyItemActive()) {
    pending_ = false;
    Apply();
  }
}

uint32_t ModelAsset::GetIcon() const {
  return IconLoader::GetIconHandle("file");
}
