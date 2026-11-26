#ifndef EDITOR_ASSET_H
#define EDITOR_ASSET_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "asset_meta.h"
#include "editor/pipelines/preview_pipeline.h"

class AssetPreview;
class ProjectAssets;

enum class AssetType {
  FALLBACK = 0,
  TEXTURE,
  FONT,
  MODEL,
  MATERIAL,
  AUDIO_CLIP,
  ANIMATION,
  OBJECT
};

class EditorAsset {
private:
  friend class ProjectAssets;

  // Asset identification
  AssetKey asset_key_;

  AssetType asset_type_;

  // Path to asset relative to project root
  std::filesystem::path asset_path_;

  // List of assets this asset depends on
  std::vector<AssetGuid> asset_dependencies_;

  // Whether asset is currently loading
  bool asset_loading_;

protected:
  EditorAsset()
      : asset_key_(),
        asset_type_(AssetType::FALLBACK),
        asset_path_(),
        asset_dependencies_(),
        asset_loading_(false) {}

public:
  virtual ~EditorAsset() = 0;

  // Returns the editor asset's key
  AssetKey GetKey() const { return asset_key_; }

  // Returns the type of the asset
  AssetType GetType() const { return asset_type_; }

  // Returns the path to the asset relative to project root path
  std::filesystem::path GetPath() const { return asset_path_; }

  // Returns the editor asset's dependencies
  const auto& GetDependencies() const { return asset_dependencies_; }

  // Returns if the asset is currently in a loading state
  bool IsLoading() const { return asset_loading_; }

  // Event when the asset is first loaded within the editor
  virtual void OnDefaultLoad(const std::filesystem::path& meta_path) = 0;

  // Event when the asset is unloaded or destroyed within the editor
  virtual void OnUnload() = 0;

  // Event when the asset is reloaded, e.g. due to file modifications
  virtual void OnReload() = 0;

  // Renders the asset's inspector UI
  virtual void DrawInspectorUI() = 0;

  // Returns the asset's icon
  virtual uint32_t GetIcon() const = 0;

  // Its preview under the inspector, as InspectableBase's; none by default
  virtual std::vector<PreviewMesh> PreviewMeshes() { return {}; }
  virtual void RenderPreviewButtons(AssetPreview& preview) {}
  virtual void RenderPreviewOverlay(AssetPreview& preview) {}
  virtual void RenderPreviewRows(AssetPreview& preview) {}
};

inline EditorAsset::~EditorAsset() {}

#endif  // EDITOR_ASSET_H