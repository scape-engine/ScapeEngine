#ifndef MODEL_ASSET_H
#define MODEL_ASSET_H

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "editor/systems/editor_asset.h"
#include "engine/renderer/model/draw_settings.h"

// A model file. Its inspector edits the model's draw settings (draw
// distance and LODs), which apply wherever the model draws
class ModelAsset : public EditorAsset {
public:
  void OnDefaultLoad(const std::filesystem::path& meta_path) override;
  void OnUnload() override {}
  void OnReload() override;
  void DrawInspectorUI() override;
  uint32_t GetIcon() const override;

private:
  // Saves the settings and applies them to the loaded model
  void Apply();

  DrawSettings draw_;
  // The LOD model paths being typed: applied when the field is left, so a
  // half-typed path never loads
  std::vector<std::array<char, 260>> sources_;
};

#endif  // MODEL_ASSET_H
