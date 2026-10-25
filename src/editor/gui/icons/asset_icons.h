#ifndef ASSET_ICONS_H
#define ASSET_ICONS_H

#include <imgui.h>
#include <string>

namespace EditorIcons {

// Kind of asset a file is, decided from its extension (case-insensitive)
enum class AssetKind { Unknown, Model, Texture, Material, Audio, Font, Data };

AssetKind GetAssetKind(const std::string& extension);

// Presentation of each kind: font icon (rows, fields) and accent color (cards)
const char* GetAssetIcon(AssetKind kind);
ImU32 GetAssetColor(AssetKind kind);

// Convenience: extension -> kind -> icon
inline const char* GetAssetIconForExtension(const std::string& extension) {
  return GetAssetIcon(GetAssetKind(extension));
}

}  // namespace EditorIcons

#endif  // ASSET_ICONS_H