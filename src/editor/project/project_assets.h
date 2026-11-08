#ifndef PROJECT_ASSETS_H
#define PROJECT_ASSETS_H

#include <bgfx/bgfx.h>
#include <string>

#include "editor/systems/editor_asset.h"
#include "engine/core/file_system_utils.h"
#include "engine/resources/guid.h"

using AssetRef = std::shared_ptr<EditorAsset>;

class ProjectAssets {

public:
  ProjectAssets();

  // Load an editor asset, returns its session id
  AssetSID Load(const FileSystem::Path& path);

  // Remove editor asset if existing
  void Remove(AssetSID id);

  // Reload an asset, return success
  bool Reload(AssetSID id);

  // Return editor asset reference by its session id, nullptr if none
  AssetRef Get(AssetSID id) const;

  // Return asset session id by its guid
  AssetSID ResolveGuid(AssetGuid& guid);

  // Update existing assets io location
  void UpdateLocation(AssetSID id, const FileSystem::Path& path);

  // Unload and forget every asset
  void Clear();

private:
  // Couner for asset session ids
  uint32_t sid_counter_;

  // Return new session id
  AssetSID CreateSID();

  // Registry of all assets by their session id
  std::unordered_map<AssetSID, AssetRef> assets_;

  // Registry of all asset session ids by their guid
  std::unordered_map<AssetGuid, AssetSID> asset_sids_;
};

#endif  // PROJECT_ASSETS_H