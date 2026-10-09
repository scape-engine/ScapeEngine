#include "entity_inspectable.h"

#include <imgui.h>

#include "editor/editor_ui.h"
#include "editor/gui/components/im_components.h"
#include "editor/gui/components/inspectable_components.h"
#include "editor/gui/hierarchy_panel.h"
#include "editor/gui/search/search_popup.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/registry/component_registry.h"

//---------------------------------------------------------------------------
// Dynamic inspector for entities
//---------------------------------------------------------------------------

EntityInspectable::EntityInspectable(HierarchyItem& item) : item(item) {}

void EntityInspectable::RenderStaticContent(ImDrawList& draw_list) {

  // Entity name header
  IMComponents::Label(item.entity.Name(), EditorStyles::GetFonts().h3_bold);
  ImGui::Dummy(ImVec2(0.0f, 3.0f));

  if (IMComponents::ButtonBig("Add Component")) {
    const ImVec2 search_position =
        ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 4.0f);
    SearchPopup::SearchComponents(search_position, item.entity.Handle());
  }
}

void EntityInspectable::RenderDynamicContent(ImDrawList& draw_list) {
  // Iterate registered components and draw their inspectors
  const auto& registry = ComponentRegistry::Get();
  const auto& keys_ordered = ComponentRegistry::OrderedKeys();

  for (const auto& key : keys_ordered) {
    auto it = registry.find(key);
    if (it != registry.end()) {
      it->second.try_draw_inspectable(item.entity.Handle());
    }
  }
}