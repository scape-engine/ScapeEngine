#include "asset_registry.h"

#include "editor/gui/inspector_panel.h"
#include "editor/systems/fallback_asset.h"
#include "editor/systems/model_asset.h"
#include "editor/systems/object_asset.h"
#include "editor/systems/texture_asset.h"
#include "engine/core/logger.h"

/**
 * TODO:
 * - Add inspect_ function to AssetInfo struct  (asset_registry.h)
 * - Modify CreateAssetInfo template to include the inspector functionality
 * (asset_registry.h)
 * - Add font asset support (we need to implement font_asset.cpp/h first)
 * (asset_registry.cpp)
 * - Connect asset inspector implementation with InspectorPanel
 */

namespace AssetRegistry {

// Global registry for all assets
std::unordered_map<std::string, std::shared_ptr<AssetInfo>> g_asset_registry_;

// Asset info for fallback asset
std::shared_ptr<AssetInfo> fallback_asset_;

// Return fallback asset
const std::shared_ptr<AssetInfo>& Fallback() {
  return fallback_asset_;
}

// Return full registry of asset types
const std::unordered_map<std::string, std::shared_ptr<AssetInfo>>& Get() {
  return g_asset_registry_;
}

// Find asset handler based on file extension
std::shared_ptr<AssetInfo> FetchByPath(const std::filesystem::path& path) {
  std::string ext = path.extension().string();  // ".png"
  if (!ext.empty() && ext.front() == '.')
    ext.erase(ext.begin());

  auto it = g_asset_registry_.find(ext);
  if (it != g_asset_registry_.end())
    return it->second;

  if (ext != "meta")  // .meta should not open anything
    return fallback_asset_;

  return nullptr;
}

// Init asset registry with all supported asset types
void Create() {
  // 1. Create fallback asset
  fallback_asset_ = CreateAssetInfo<FallbackAsset>(AssetType::FALLBACK);

  // Register every asset type
  RegisterAsset<TextureAsset>(
      AssetType::TEXTURE, {"png", "jpg", "jpeg", "bmp", "tga", "gif", "hdr"});
  RegisterAsset<ModelAsset>(AssetType::MODEL, {"glb", "gltf", "fbx", "obj"});
  RegisterAsset<ObjectAsset>(AssetType::OBJECT, {"object"});

  // TODO: add more asset types here when needed

  // TODO: missing font_asset implementation
  /* RegisterAsset<FontAsset>(AssetType::FONT, {"ttf", "otf"}); */
}

}  // namespace AssetRegistry
