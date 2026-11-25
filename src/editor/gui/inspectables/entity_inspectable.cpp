#include "entity_inspectable.h"

#include <imgui.h>

#include "editor/editor_ui.h"
#include "editor/gui/components/im_components.h"
#include "editor/gui/components/inspectable_components.h"
#include "editor/gui/hierarchy_panel.h"
#include "editor/gui/search/search_popup.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/registry/component_registry.h"
#include "editor/runtime/runtime.h"
#include "engine/pcg/stage.h"

//---------------------------------------------------------------------------
// Dynamic inspector for entities
//---------------------------------------------------------------------------

EntityInspectable::EntityInspectable(HierarchyItem& item) : item(item) {}

// Generator > stage, the generator selectable
static void _DrawOrigin(const InspectableComponents::GeneratedOrigin& origin) {
  ImGui::PushFont(EditorStyles::GetFonts().s);
  const std::string& name =
      ECS::Main().Get<TransformComponent>(origin.volume).name_;
  const ImVec2 position = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##generator", ImGui::CalcTextSize(name.c_str()));
  const bool hovered = ImGui::IsItemHovered();
  if (hovered)
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  if (ImGui::IsItemClicked())
    Runtime::State().SelectEntity(origin.volume);
  ImGui::GetWindowDrawList()->AddText(
      position, hovered ? EditorColor::accent_hover : EditorColor::text,
      name.c_str());

  if (const PCG::Stage* stage = InspectableComponents::FindStage(origin)) {
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, EditorColor::text_dim);
    ImGui::TextUnformatted(ICON_FA_CHEVRON_RIGHT);
    ImGui::SameLine();
    ImGui::TextUnformatted(
        (stage->name.empty() ? origin.stage : stage->name).c_str());
    ImGui::PopStyleColor();
  }
  ImGui::PopFont();
}

void EntityInspectable::RenderStaticContent(ImDrawList& draw_list) {

  // Entity name header
  IMComponents::Label(item.entity.Name(), EditorStyles::GetFonts().h3_bold);
  // Generated: where it comes from instead, as only its generator adds to it
  const auto origin = InspectableComponents::FindOrigin(item.entity.Handle());
  if (origin.volume != entt::null) {
    _DrawOrigin(origin);
    return;
  }
  ImGui::Dummy(ImVec2(0.0f, 3.0f));

  IMComponents::BeginCentered("AddComponent");
  if (IMComponents::ButtonBig(ICON_FA_PLUS "  Add Component")) {
    const ImVec2 search_position =
        ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 4.0f);
    SearchPopup::SearchComponents(search_position, item.entity.Handle());
  }
  IMComponents::EndCentered();
}

void EntityInspectable::RenderDynamicContent(ImDrawList& draw_list) {
  // Iterate registered components and draw their inspectors
  const auto& registry = ComponentRegistry::Get();
  const auto& keys_ordered = ComponentRegistry::OrderedKeys();

  InspectableComponents::AllowRemoving(
      InspectableComponents::FindOrigin(item.entity.Handle()).volume ==
      entt::null);
  for (const auto& key : keys_ordered) {
    auto it = registry.find(key);
    if (it != registry.end()) {
      it->second.try_draw_inspectable(item.entity.Handle());
    }
  }
  InspectableComponents::AllowRemoving(true);
}