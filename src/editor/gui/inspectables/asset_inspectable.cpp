#include "asset_inspectable.h"

#include "editor/gui/components/im_components.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/runtime/runtime.h"

AssetInspectable::AssetInspectable(AssetSID asset_id) : asset_id_(asset_id) {
  // store SID
}

// Draw once when InspectableBase is opened
void AssetInspectable::RenderStaticContent(ImDrawList& draw_list) {
  if (auto asset = Runtime::Project().Assets().Get(asset_id_)) {
    IMComponents::Label(asset->GetPath().filename().string(),
                        EditorStyles::GetFonts().h3_bold);
    IMComponents::Label(asset->GetPath().generic_string(),
                        EditorStyles::GetFonts().h4,
                        IM_COL32(210, 210, 255, 255));
    ImGui::Dummy(ImVec2(0.0f, 3.0f));
  } else {
    ImGui::TextDisabled("Asset not found.");
  }
}

void AssetInspectable::RenderDynamicContent(ImDrawList& draw_list) {
  if (auto asset = Runtime::Project().Assets().Get(asset_id_))
    asset->DrawInspectorUI();
}
