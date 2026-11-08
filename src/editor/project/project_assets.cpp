#include "project_assets.h"

#include "editor/registry/asset_registry.h"
#include "editor/runtime/runtime.h"
#include "editor/systems/asset_meta.h"
#include "engine/core/logger.h"

ProjectAssets::ProjectAssets() : sid_counter_(0), assets_(), asset_sids_() {}

AssetSID ProjectAssets::Load(const FileSystem::Path& path) {
  // Get absolute path from project root
  FileSystem::Path absolute_path = Runtime::Project().AbsolutePath(path);

  // PREPARE ASSET
  // Try to fetch asset type info
  auto asset_info = AssetRegistry::FetchByPath(path);
  if (!asset_info)
    return 0;  // If unknown asset type, don't create fallback

  // Create asset instance
  AssetRef asset = asset_info->create_instance_();
  asset->asset_type_ = asset_info->type_;
  asset->asset_path_ = path;
  asset->asset_key_.sid_ = CreateSID();

  // LOAD OR CREATE METADATA
  FileSystem::Path meta_path = absolute_path.string() + ".meta";

  if (!FileSystem::Exists(meta_path)) {
    asset->asset_key_.guid_ = XG::createGUID();

    // Create metadata file
    std::ofstream meta_file(meta_path);
    if (!meta_file.is_open())
      return 0;

    // Write metadata header
    meta_file << AssetMeta::CreateHeader(asset->asset_key_.guid_);
    meta_file.close();
  } else {
    asset->asset_key_.guid_ = AssetMeta::ParseGuid(meta_path);
  }

  // Ensure guid was parsed or generated
  if (!asset->asset_key_.guid_.isValid())
    return 0;

  // REGISTER ASSET
  assets_[asset->asset_key_.sid_] = asset;
  asset_sids_[asset->asset_key_.guid_] = asset->asset_key_.sid_;

  // ASSET LOAD EVENT
  asset->OnDefaultLoad(meta_path);

  return asset->asset_key_.sid_;
}

void ProjectAssets::Remove(AssetSID id) {
  // Try to find asset
  auto it = assets_.find(id);
  if (it == assets_.end())
    return;

  AssetRef asset = it->second;

  // Cache assets path
  FileSystem::Path absolute_path =
      Runtime::Project().AbsolutePath(asset->asset_path_);

  // Unload asset and remove it
  asset->OnUnload();
  // Remove from assets maps
  asset_sids_.erase(asset->asset_key_.guid_);
  assets_.erase(it);

  // Delete assets metafile if existing
  FileSystem::Path meta_path = absolute_path.string() + ".meta";
  if (FileSystem::Exists(meta_path)) {
    std::error_code ec;
    std::filesystem::remove(meta_path, ec);
    if (ec) {
      Logger::getInstance().Log(LogLevel::Warning,
                                "Failed to remove metadata file: " +
                                    meta_path.string() + " - " + ec.message());
    }
  }
}

bool ProjectAssets::Reload(AssetSID id) {
  // Try to find asset
  auto it = assets_.find(id);
  if (it == assets_.end())
    return false;

  AssetRef asset = it->second;

  // Reload asset
  asset->OnReload();
  return true;
}

AssetRef ProjectAssets::Get(AssetSID id) const {
  auto it = assets_.find(id);
  if (it != assets_.end())
    return it->second;
  return nullptr;
}

AssetSID ProjectAssets::ResolveGuid(AssetGuid& guid) {
  auto it = asset_sids_.find(guid);
  if (it != asset_sids_.end())
    return it->second;
  return 0;
}

void ProjectAssets::UpdateLocation(AssetSID id, const FileSystem::Path& path) {
  auto it = assets_.find(id);
  if (it == assets_.end())
    return;

  AssetRef asset = it->second;

  // Cache old relative path and update it
  FileSystem::Path old_relative_path = asset->asset_path_;
  asset->asset_path_ = path;

  // Cache old and current absolute path
  FileSystem::Path old_absolute_path =
      Runtime::Project().AbsolutePath(old_relative_path);
  FileSystem::Path absolute_path = Runtime::Project().AbsolutePath(path);

  // Move the meta file
  std::error_code ec;
  std::filesystem::rename(old_absolute_path.string() + ".meta",
                          absolute_path.string() + ".meta", ec);

  if (ec) {
    Logger::getInstance().Log(LogLevel::Warning,
                              "Failed to move metadata file: " + ec.message());
  }
}

void ProjectAssets::Clear() {
  for (auto& [sid, asset] : assets_)
    asset->OnUnload();
  assets_.clear();
  asset_sids_.clear();
}

AssetSID ProjectAssets::CreateSID() {
  return ++sid_counter_;
}