#include "asset_drag.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace AssetDrag {

static constexpr const char* payload_type = "ASSET_FILE";

static std::string _Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return text;
}

void Source(const std::string& path) {
  if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    return;
  ImGui::SetDragDropPayload(payload_type, path.c_str(), path.size() + 1);
  ImGui::Text("%s", std::filesystem::path(path).filename().string().c_str());
  ImGui::EndDragDropSource();
}

const ImGuiPayload* Peek(const std::vector<std::string>& extensions) {
  const ImGuiPayload* payload = ImGui::GetDragDropPayload();
  if (!payload || !payload->IsDataType(payload_type))
    return nullptr;
  const std::string extension =
      _Lower(std::filesystem::path(Path(*payload)).extension().string());
  for (const std::string& wanted : extensions)
    if (_Lower(wanted) == extension)
      return payload;
  return nullptr;
}

const ImGuiPayload* Accept(const std::vector<std::string>& extensions,
                           ImGuiDragDropFlags flags) {
  return Peek(extensions) ? ImGui::AcceptDragDropPayload(payload_type, flags)
                          : nullptr;
}

std::string Path(const ImGuiPayload& payload) {
  return static_cast<const char*>(payload.Data);
}

}  // namespace AssetDrag
