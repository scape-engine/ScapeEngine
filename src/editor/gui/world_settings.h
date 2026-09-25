#ifndef WORLD_SETTINGS_H
#define WORLD_SETTINGS_H

#include <imgui.h>
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"
#include "editor/runtime/runtime.h"
#include "engine/renderer/skybox/skybox.h"

namespace Panels {

inline void WorldSettings() {
  // Self-contained window
  ImGui::Begin(ICON_FA_GLOBE " World");

  Skybox* skybox = Runtime::BuildGlobalResources().skybox;
  if (!skybox) {
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() * 0.4f);
    ImGui::TextDisabled("No skybox available");
    ImGui::End();
    return;
  }

  // ═══════════════════════════════════════════════════════════
  //  SKY & ATMOSPHERE
  // ═══════════════════════════════════════════════════════════
  if (ImGui::CollapsingHeader(ICON_FA_CLOUD " Sky & Atmosphere",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Indent();

    ImGui::SeparatorText("Time of Day");
    ImGui::Text("Current Time: %s", skybox->GetTimeString().c_str());

    float timeOfDay = skybox->GetTimeOfDay();
    float hours = timeOfDay * 24.0f;
    if (ImGui::SliderFloat("##TimeSlider", &hours, 0.0f, 24.0f, "%.1f h")) {
      skybox->SetTimeOfDay(hours / 24.0f);
    }

    ImGui::Spacing();

    bool paused = skybox->IsCyclePaused();
    if (ImGui::Checkbox("Pause Cycle", &paused)) {
      skybox->SetCyclePaused(paused);
    }

    if (!paused) {
      float speed = skybox->GetCycleSpeed();
      if (ImGui::SliderFloat("Cycle Speed", &speed, 0.0f, 1.0f, "%.3f")) {
        skybox->SetCycleSpeed(speed);
      }
      ImGui::SameLine();
      if (ImGui::Button(ICON_FA_ROTATE_LEFT "##Speed")) {
        skybox->SetCycleSpeed(0.05f);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reset Speed");
    }

    ImGui::Spacing();
    ImGui::Text("Presets:");
    ImGui::SameLine();
    if (ImGui::SmallButton("Dawn"))
      skybox->SetTimeOfDay(0.25f);
    ImGui::SameLine();
    if (ImGui::SmallButton("Noon"))
      skybox->SetTimeOfDay(0.5f);
    ImGui::SameLine();
    if (ImGui::SmallButton("Dusk"))
      skybox->SetTimeOfDay(0.75f);
    ImGui::SameLine();
    if (ImGui::SmallButton("Midnight"))
      skybox->SetTimeOfDay(0.0f);

    ImGui::Separator();

    if (ImGui::TreeNode("Advanced Sky Parameters")) {
      const auto& params = skybox->GetParams();

      float sunRadius = params._parametersArray[0];
      float bloom = params._parametersArray[1];
      float exposure = params._parametersArray[2];

      bool changed = false;
      changed |=
          ImGui::SliderFloat("Sun Radius", &sunRadius, 0.001f, 0.05f, "%.4f");
      changed |= ImGui::SliderFloat("Bloom", &bloom, 0.0f, 5.0f);
      changed |= ImGui::SliderFloat("Exposure", &exposure, 0.01f, 2.0f);

      if (changed)
        skybox->SetParams(sunRadius, bloom, exposure);

      if (ImGui::Button(ICON_FA_ROTATE_LEFT " Reset Defaults")) {
        skybox->SetParams(0.00465f, 1.0f, 0.25f);
      }
      ImGui::TreePop();
    }
    ImGui::Unindent();
  }

  // ═══════════════════════════════════════════════════════════
  //  LIGHTING
  // ═══════════════════════════════════════════════════════════
  if (ImGui::CollapsingHeader(ICON_FA_LIGHTBULB " Lighting")) {
    ImGui::Indent();
    const auto& params = skybox->GetParams();

    ImGui::Text("Sun Direction: (%.2f, %.2f, %.2f)", params.sunDirShader[0],
                params.sunDirShader[1], params.sunDirShader[2]);
    ImGui::ColorEdit3(
        "Sun Color", (float*)params._sunColorArray,
        ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_NoInputs);
    ImGui::Text("Sun Intensity: %.2f", params._sunColorArray[3]);

    ImGui::Spacing();
    ImGui::ColorEdit3(
        "Ambient Color", (float*)params._skyAmbientArray,
        ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_NoInputs);
    ImGui::TextDisabled("(Computed from time of day)");
    ImGui::Unindent();
  }

  // ═══════════════════════════════════════════════════════════
  //  RENDERING
  // ═══════════════════════════════════════════════════════════
  if (ImGui::CollapsingHeader(ICON_FA_DESKTOP " Rendering")) {
    ImGui::Indent();
    auto& pipeline = Runtime::GetSceneViewPipeline();
    ImGui::Checkbox("Wireframe Mode", &pipeline.wireframe_);
    ImGui::Checkbox("Show Skybox", &pipeline.show_skybox_);
    ImGui::Checkbox("Show Gizmos", &pipeline.show_gizmos_);
    ImGui::Unindent();
  }

  // ═══════════════════════════════════════════════════════════
  //  POST-PROCESSING
  // ═══════════════════════════════════════════════════════════
  if (ImGui::CollapsingHeader(ICON_FA_WAND_MAGIC_SPARKLES " Post-Processing")) {
    ImGui::Indent();
    ImGui::TextDisabled("Coming soon...");
    ImGui::Unindent();
  }

  ImGui::End();
}

}  // namespace Panels

#endif  // WORLD_SETTINGS_H