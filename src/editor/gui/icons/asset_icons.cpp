#include "asset_icons.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"

namespace EditorIcons {

AssetKind GetAssetKind(const std::string& extension) {
  static const std::unordered_map<std::string, AssetKind> kinds = {
      {".gltf", AssetKind::Model},        {".glb", AssetKind::Model},
      {".fbx", AssetKind::Model},         {".obj", AssetKind::Model},
      {".png", AssetKind::Texture},       {".jpg", AssetKind::Texture},
      {".jpeg", AssetKind::Texture},      {".tga", AssetKind::Texture},
      {".hdr", AssetKind::Texture},       {".mat", AssetKind::Material},
      {".material", AssetKind::Material}, {".wav", AssetKind::Audio},
      {".ogg", AssetKind::Audio},         {".ttf", AssetKind::Font},
      {".json", AssetKind::Data},
  };

  std::string key = extension;
  std::transform(key.begin(), key.end(), key.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  const auto it = kinds.find(key);
  return it != kinds.end() ? it->second : AssetKind::Unknown;
}

const char* GetAssetIcon(AssetKind kind) {
  switch (kind) {
    case AssetKind::Model:
      return ICON_FA_CUBE;
    case AssetKind::Texture:
      return ICON_FA_IMAGE;
    case AssetKind::Material:
      return ICON_FA_CIRCLE_HALF_STROKE;
    case AssetKind::Audio:
      return ICON_FA_MUSIC;
    case AssetKind::Font:
      return ICON_FA_FONT;
    case AssetKind::Data:
      return ICON_FA_FILE_CODE;
    default:
      return ICON_FA_FILE;
  }
}

ImU32 GetAssetColor(AssetKind kind) {
  switch (kind) {
    case AssetKind::Model:
      return IM_COL32(45, 180, 200, 255);  // cyan
    case AssetKind::Texture:
      return IM_COL32(180, 45, 45, 255);  // red
    case AssetKind::Material:
      return IM_COL32(45, 200, 45, 255);  // green
    case AssetKind::Data:
      return IM_COL32(200, 180, 45, 255);  // yellow
    default:
      return IM_COL32(100, 100, 100, 255);  // gray
  }
}

}  // namespace EditorIcons