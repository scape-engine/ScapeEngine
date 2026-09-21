#ifndef WORLD_SETTINGS_H
#define WORLD_SETTINGS_H

#include <imgui.h>
#include "editor/runtime/runtime.h"
#include "engine/renderer/skybox/skybox.h"

namespace Panels {

inline void WorldSettings() {
  ImGui::BeginChild("WorldSettings", ImVec2(0, 0), false);

  // Get global skybox
  Skybox* skybox = Runtime::BuildGlobalResources().skybox;
  if (!skybox) {
    ImGui::TextDisabled("No skybox available");
    ImGui::EndChild();
    return;
  }

  // ═══════════════════════════════════════════════════════════
  //  SKY & ATMOSPHERE
  // ═══════════════════════════════════════════════════════════
  if (ImGui::CollapsingHeader("Sky & Atmosphere", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Indent();
    
    // Time of Day controls
    ImGui::SeparatorText("Time of Day");
    
    // Display current time
    ImGui::Text("Current Time: %s", skybox->GetTimeString().c_str());
    
    // Time slider (0-24 hours)
    float timeOfDay = skybox->GetTimeOfDay();
    float hours = timeOfDay * 24.0f;
    if (ImGui::SliderFloat("##TimeSlider", &hours, 0.0f, 24.0f, "%.1f h")) {
      skybox->SetTimeOfDay(hours / 24.0f);
    }
    
    ImGui::Spacing();
    
    // Cycle controls
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
      if (ImGui::Button("Reset##Speed")) {
        skybox->SetCycleSpeed(0.05f);
      }
    }
    
    // Time presets
    ImGui::Spacing();
    ImGui::Text("Presets:");
    ImGui::SameLine();
    if (ImGui::SmallButton("Dawn"))    skybox->SetTimeOfDay(0.25f);
    ImGui::SameLine();
    if (ImGui::SmallButton("Noon"))    skybox->SetTimeOfDay(0.5f);
    ImGui::SameLine();
    if (ImGui::SmallButton("Dusk"))    skybox->SetTimeOfDay(0.75f);
    ImGui::SameLine();
    if (ImGui::SmallButton("Midnight")) skybox->SetTimeOfDay(0.0f);
    
    ImGui::Separator();
    
    // Advanced sky parameters
    if (ImGui::TreeNode("Advanced Sky Parameters")) {
      const auto& params = skybox->GetParams();
      
      // Get mutable copy for editing
      float sunRadius = params._parametersArray[0];
      float bloom = params._parametersArray[1];
      float exposure = params._parametersArray[2];
      
      bool changed = false;
      changed |= ImGui::SliderFloat("Sun Angular Radius", &sunRadius, 0.001f, 0.05f, "%.4f");
      changed |= ImGui::SliderFloat("Bloom", &bloom, 0.0f, 5.0f);
      changed |= ImGui::SliderFloat("Exposure", &exposure, 0.01f, 2.0f);
      
      if (changed) {
        skybox->SetParams(sunRadius, bloom, exposure);
      }
      
      if (ImGui::Button("Reset to Defaults")) {
        skybox->SetParams(0.00465f, 1.0f, 0.25f);
      }
      
      ImGui::TreePop();
    }
    
    ImGui::Unindent();
  }
  
  // ═══════════════════════════════════════════════════════════
  //  LIGHTING
  // ═══════════════════════════════════════════════════════════
  if (ImGui::CollapsingHeader("Lighting")) {
    ImGui::Indent();
    
    const auto& params = skybox->GetParams();
    
    ImGui::Text("Sun Direction: (%.2f, %.2f, %.2f)", 
                params.sunDirShader[0], 
                params.sunDirShader[1], 
                params.sunDirShader[2]);
    
    ImGui::ColorEdit3("Sun Color", (float*)params._sunColorArray, 
                     ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_NoInputs);
    
    ImGui::Text("Sun Intensity: %.2f", params._sunColorArray[3]);
    
    ImGui::Spacing();
    ImGui::Text("Ambient Light:");
    ImGui::ColorEdit3("Ambient Color", (float*)params._skyAmbientArray, 
                     ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_NoInputs);
    
    ImGui::TextDisabled("(These are computed from time of day)");
    
    ImGui::Unindent();
  }
  
  // ═══════════════════════════════════════════════════════════
  //  RENDERING
  // ═══════════════════════════════════════════════════════════
  if (ImGui::CollapsingHeader("Rendering")) {
    ImGui::Indent();
    
    auto& pipeline = Runtime::GetSceneViewPipeline();
    
    bool wireframe = pipeline.wireframe_;
    if (ImGui::Checkbox("Wireframe Mode", &wireframe)) {
      pipeline.wireframe_ = wireframe;
    }
    
    bool showSkybox = pipeline.show_skybox_;
    if (ImGui::Checkbox("Show Skybox", &showSkybox)) {
      pipeline.show_skybox_ = showSkybox;
    }
    
    bool showGizmos = pipeline.show_gizmos_;
    if (ImGui::Checkbox("Show Gizmos", &showGizmos)) {
      pipeline.show_gizmos_ = showGizmos;
    }
    
    ImGui::Unindent();
  }
  
  // ═══════════════════════════════════════════════════════════
  //  TODO: POST-PROCESSING
  // ═══════════════════════════════════════════════════════════
  if (ImGui::CollapsingHeader("Post-Processing")) {
    ImGui::Indent();
    ImGui::TextDisabled("Coming soon...");
    // TODO: Add bloom, tonemapping, color grading, etc.
    ImGui::Unindent();
  }
  
  ImGui::EndChild();
}

}  // namespace Panels

#endif  // WORLD_SETTINGS_H