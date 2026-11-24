#ifndef OBJECT_ASSET_H
#define OBJECT_ASSET_H

#include <cstdint>
#include <filesystem>
#include <string>

#include "api/region.h"
#include "editor/systems/editor_asset.h"

// A kind of object procedural content places (a .object file: worldgen's
// RegionObject as JSON): the model it draws, the size of the regions that
// place it and its placement rules. Its inspector edits and saves it, and
// rebuilds the planets that place it
class ObjectAsset : public EditorAsset {
public:
  void OnDefaultLoad(const std::filesystem::path& meta_path) override;
  void OnUnload() override {}
  void OnReload() override;
  void DrawInspectorUI() override;
  uint32_t GetIcon() const override;

private:
  void Save();

  Worldgen::RegionObject object_;
  std::string error_;  // why the file didn't load; it isn't saved over then
  bool pending_ = false;  // edited, saved once no field is being edited
};

#endif  // OBJECT_ASSET_H
