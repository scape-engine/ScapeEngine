#ifndef TERRAIN_EDITOR_H
#define TERRAIN_EDITOR_H

#include <imgui.h>
#include <functional>

#include "engine/pcg/constraints/biome_presets.h"
#include "engine/pcg/core/feature_descriptors.h"
#include "engine/pcg/terrain/terrain_generator.h"

namespace Panels {

inline void TerrainEditor() {
  static PCG::Config config = []() {
    PCG::Config cfg;
    cfg.seed = 42;
    cfg.frequency = 0.01f;
    cfg.amplitude = 10.0f;
    cfg.octaves = 4;
    cfg.lacunarity = 2.0f;
    cfg.gain = 0.5f;
    cfg.sharpness = 0.3f;
    PCG::BiomePresets::ApplyTemperateRules(cfg.constraints);
    return cfg;
  }();

  // Get game instance
  GameManager* manager = Runtime::Game();
  if (!manager)
    return;

  GameBase* base = manager->GetGame();
  GameTest* game = dynamic_cast<GameTest*>(base);
  if (!game)
    return;

  auto& generator = game->GetGenerator();

  static uint16_t gridSize = 512;  // Add grid size control
  static bool regenerate_requested = false;

  // Mesh Settings
  if (ImGui::CollapsingHeader("Mesh Settings",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    int gridSize_int = gridSize;
    if (ImGui::SliderInt("Grid Size", &gridSize_int, 64, 4096)) {
      gridSize = (uint16_t)gridSize_int;
      regenerate_requested = true;
    }
    ImGui::Text("Total Vertices: %d", gridSize * gridSize);
    ImGui::Text("Total Triangles: %d", (gridSize - 1) * (gridSize - 1) * 2);
  }

  // Base Parameters
  if (ImGui::CollapsingHeader("Base Parameters",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    bool changed = false;

    changed |= ImGui::DragInt("Seed", (int*)&config.seed, 1.0f, 0, 999999);
    changed |=
        ImGui::DragFloat("Frequency", &config.frequency, 0.001f, 0.001f, 0.1f);
    changed |=
        ImGui::DragFloat("Amplitude", &config.amplitude, 1.0f, 1.0f, 100.0f);
    changed |= ImGui::DragInt("Octaves", &config.octaves, 1, 1, 8);
    changed |=
        ImGui::DragFloat("Lacunarity", &config.lacunarity, 0.1f, 1.0f, 4.0f);
    changed |= ImGui::DragFloat("Gain", &config.gain, 0.05f, 0.0f, 1.0f);
    changed |=
        ImGui::DragFloat("Sharpness", &config.sharpness, 0.05f, 0.0f, 1.0f);

    if (changed) {
      regenerate_requested = true;
    }
  }

  // Erosion Parameters
  if (ImGui::CollapsingHeader("Erosion")) {
    bool changed = false;
    changed |= ImGui::DragFloat("Altitude", &config.altitude_erosion, 0.05f,
                                0.0f, 1.0f);
    changed |=
        ImGui::DragFloat("Ridge", &config.ridge_erosion, 0.05f, 0.0f, 1.0f);
    changed |=
        ImGui::DragFloat("Slope", &config.slope_erosion, 0.05f, 0.0f, 1.0f);

    if (changed) {
      regenerate_requested = true;
    }
  }

  if (ImGui::CollapsingHeader("Parameter Variation")) {
    ImGui::Spacing();

    bool changed = false;

    // Sharpness variation
    ImGui::PushID("vary_sharpness");
    changed |= IMComponents::Checkbox("Vary Sharpness",
                                      &config.vary_sharpness.enabled);
    if (config.vary_sharpness.enabled) {
      ImGui::Indent();

      if (ImGui::SliderFloat("Variation Speed",
                             &config.vary_sharpness.frequency, 0.001f, 0.1f,
                             "%.4f")) {
        changed = true;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How quickly sharpness changes across terrain");
      }

      if (ImGui::SliderFloat("Variation Amount",
                             &config.vary_sharpness.amplitude, 0.0f, 1.0f,
                             "%.2f")) {
        changed = true;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How much sharpness varies (±amplitude)");
      }

      ImGui::Unindent();
    }
    ImGui::PopID();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Perturbation variation
    ImGui::PushID("vary_perturb");
    changed |= IMComponents::Checkbox("Vary Domain Warp",
                                      &config.vary_perturb.enabled);
    if (config.vary_perturb.enabled) {
      ImGui::Indent();

      if (ImGui::SliderFloat("Variation Speed", &config.vary_perturb.frequency,
                             0.001f, 0.1f, "%.4f")) {
        changed = true;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How quickly warp strength changes");
      }

      if (ImGui::SliderFloat("Variation Amount", &config.vary_perturb.amplitude,
                             0.0f, 1.0f, "%.2f")) {
        changed = true;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How much warp varies (±amplitude)");
      }

      ImGui::Unindent();
    }
    ImGui::PopID();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Feature amplification variation
    ImGui::PushID("vary_amplify");
    changed |= IMComponents::Checkbox("Vary Feature Emphasis",
                                      &config.vary_amplify.enabled);
    if (config.vary_amplify.enabled) {
      ImGui::Indent();

      if (ImGui::SliderFloat("Variation Speed", &config.vary_amplify.frequency,
                             0.001f, 0.1f, "%.4f")) {
        changed = true;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How quickly feature emphasis changes");
      }

      if (ImGui::SliderFloat("Variation Amount", &config.vary_amplify.amplitude,
                             0.0f, 1.0f, "%.2f")) {
        changed = true;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How much emphasis varies (±amplitude)");
      }

      ImGui::Unindent();
    }
    ImGui::PopID();

    if (changed) {
      regenerate_requested = true;
    }
  }

  // Feature Descriptors
  if (ImGui::CollapsingHeader("Features")) {
    ImGui::Text("Add features at specific locations:");
    ImGui::Spacing();

    // Add feature
    if (ImGui::Button("Add Mountain")) {
      config.features.push_back(PCG::Descriptors::Mountains(
          glm::vec2(gridSize / 2.0f, gridSize / 2.0f), 150.0f));
      regenerate_requested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Plains")) {
      config.features.push_back(PCG::Descriptors::Plains(
          glm::vec2(gridSize / 2.0f, gridSize / 2.0f), 200.0f));
      regenerate_requested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Valley")) {
      config.features.push_back(PCG::Descriptors::Valleys(
          glm::vec2(gridSize / 2.0f, gridSize / 2.0f), 180.0f));
      regenerate_requested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Hills")) {
      config.features.push_back(PCG::Descriptors::Hills(
          glm::vec2(gridSize / 2.0f, gridSize / 2.0f), 250.0f));
      regenerate_requested = true;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Display current features
    if (config.features.empty()) {
      ImGui::TextDisabled("No features added yet");
    } else {
      ImGui::Text("Active Features (%zu):", config.features.size());

      // Iterate through features
      // TODO: need index for deletion
      for (int i = 0; i < (int)config.features.size(); ++i) {
        auto& feature = config.features[i];

        ImGui::PushID(i);
        bool changed = false;

        // Feature header
        if (ImGui::TreeNode("##feature", "Feature #%d", i + 1)) {
          // Position
          changed |= ImGui::DragFloat2("Center", &feature.center.x, 1.0f, 0.0f,
                                       (float)gridSize);

          // Influence
          changed |=
              ImGui::DragFloat("Radius", &feature.radius, 1.0f, 10.0f, 500.0f);
          changed |= ImGui::DragFloat("Strength", &feature.strength, 0.05f,
                                      0.0f, 1.0f);

          // Noise params
          changed |= ImGui::DragFloat("Frequency", &feature.frequency, 0.001f,
                                      0.001f, 0.1f);
          changed |= ImGui::DragFloat("Amplitude", &feature.amplitude, 1.0f,
                                      -100.0f, 100.0f);
          changed |= ImGui::DragInt("Octaves", &feature.octaves, 1, 1, 8);
          changed |= ImGui::DragFloat("Sharpness", &feature.sharpness, 0.05f,
                                      0.0f, 1.0f);

          ImGui::Spacing();

          // Delete
          if (ImGui::Button("Delete Feature", ImVec2(-1, 0))) {
            config.features.erase(config.features.begin() + i);
            regenerate_requested = true;
            ImGui::TreePop();
            ImGui::PopID();
            break;  // Exit loop
          }

          ImGui::TreePop();
        }

        if (changed) {
          regenerate_requested = true;
        }

        ImGui::PopID();
      }
    }

    // Clear all
    if (!config.features.empty()) {
      ImGui::Spacing();
      if (ImGui::Button("Clear All Features")) {
        config.features.clear();
        regenerate_requested = true;
      }
    }
  }

  // Biomes
  if (ImGui::CollapsingHeader("Biomes")) {
    if (ImGui::Button("Temperate")) {
      config.constraints = PCG::ConstraintSystem();
      PCG::BiomePresets::ApplyTemperateRules(config.constraints);
      regenerate_requested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Desert")) {
      config.constraints = PCG::ConstraintSystem();
      PCG::BiomePresets::ApplyDesertRules(config.constraints);
      regenerate_requested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Arctic")) {
      config.constraints = PCG::ConstraintSystem();
      PCG::BiomePresets::ApplyArcticRules(config.constraints);
      regenerate_requested = true;
    }
  }

  // Debug: Pipeline Stages
  if (ImGui::CollapsingHeader("Debug: Pipeline Stages")) {
    bool changed = false;

    const char* stage_names[] = {"Base Noise Only",    "With Domain Warp",
                                 "With Sharpness",     "With Slope Erosion",
                                 "With Ridge Erosion", "With Altitude Erosion",
                                 "Complete Pipeline"};

    int current_stage = static_cast<int>(config.debug_stage);
    if (ImGui::Combo("Active Stage", &current_stage, stage_names, 7)) {
      config.debug_stage = static_cast<PCG::PipelineStage>(current_stage);
      changed = true;
    }

    ImGui::Spacing();
    ImGui::TextWrapped(
        "Isolate each pipeline stage for evaluation"
        "Set 'Complete Pipeline' for normal operation");
    if (changed) {
      regenerate_requested = true;
    }
  }

  // Generate button
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (ImGui::Button("Generate Terrain")) {
    regenerate_requested = true;
  }

  if (regenerate_requested) {
    // game->GenerateTerrain(config, gridSize);
    regenerate_requested = false;
  }
}

}  // namespace Panels

#endif  // TERRAIN_EDITOR_H