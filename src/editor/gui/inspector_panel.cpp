#include "inspector_panel.h"

#include <imgui_internal.h>

#include <algorithm>

#include "editor/gui/components/im_components.h"
#include "editor/gui/hierarchy_panel.h"
#include "editor/gui/inspectables/asset_inspectable.h"
#include "editor/gui/inspectables/entity_inspectable.h"
#include "editor/gui/utils/gui_utils.h"
#include "editor/runtime/runtime.h"
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"

InspectorPanel::InspectorPanel() {}

void InspectorPanel::Render() {
  // ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 20.0f));
  GUIUtils::HideDockTabBar();
  ImGui::Begin("Inspector", nullptr, EditorFlag::fixed);
  {
    ImDrawList& draw_list = *ImGui::GetWindowDrawList();

    // Drive inspector from EditorState selection
    auto& state = Runtime::State();
    auto& world = ECS::Main();
    Entity selection = state.selected_entity;

    if (selection != entt::null && world.Reg().valid(selection)) {
      // Only re-inspect if selection changed
      if (selection != last_inspected_entity_) {
        last_inspected_entity_ = selection;
        last_inspected_asset_ = 0;

        // Keep the HierarchyItem alive
        HierarchyItem item{EntityContainer(selection)};
        Inspect<EntityInspectable>(item);
      }
    } else if (state.selected_asset != 0) {
      if (state.selected_asset != last_inspected_asset_) {
        last_inspected_asset_ = state.selected_asset;
        last_inspected_entity_ = entt::null;
        Inspect<AssetInspectable>(state.selected_asset);
      }
    } else {
      if (last_inspected_entity_ != entt::null || last_inspected_asset_ != 0) {
        last_inspected_entity_ = entt::null;
        last_inspected_asset_ = 0;
        inspected_ = nullptr;
      }
    }

    // Render inspected static content if available
    if (inspected_) {
      inspected_->RenderStaticContent(draw_list);
    }

    // The preview, while the inspected has something to show; a new item
    // starts from the first view
    const bool rendering_preview =
        inspected_ && !inspected_->PreviewMeshes().empty();
    if (inspected_.get() != previewed_) {
      previewed_ = inspected_.get();
      preview_.Reset();
      viewer_.Reset();
    }

    ImGui::Dummy(ImVec2(0.0f, 2.0f));

    // The content above the preview's header and, while open here, it
    ImVec2 content_size = ImGui::GetContentRegionAvail();
    if (rendering_preview) {
      content_size.y -= ImGui::GetFrameHeight() + 8.0f +
                        EditorSizes::window_padding;
      if (preview_open_ && !popped_out_)
        content_size.y -= preview_height_;
    }

    // Render inspected content if available
    if (inspected_) {

      // Inspect content child
      IMComponents::BeginClippedChild("##InspectorContent", content_size);
      { inspected_->RenderDynamicContent(draw_list); }
      IMComponents::EndClippedChild();

      if (rendering_preview)
        RenderPreview(draw_list);

    } else {
      RenderNoneInspected();
    }

    // Any widget of this window edited this frame
    // Catches asset inspectables
    const ImGuiContext& g = *GImGui;
    if (g.ActiveIdHasBeenEditedThisFrame && g.ActiveIdWindow &&
        g.ActiveIdWindow->RootWindow == ImGui::GetCurrentWindow()->RootWindow)
      Runtime::Project().MarkSceneDirty();
  }
  ImGui::End();

  if (popped_out_)
    RenderViewer();
}

void InspectorPanel::RenderNoneInspected() {
  IMComponents::Label("Nothing selected", EditorStyles::GetFonts().h3_bold);
  IMComponents::Label("Select an entity or asset to inspect and edit it.",
                      EditorStyles::GetFonts().h4,
                      IM_COL32(210, 210, 255, 255));
}

void InspectorPanel::RenderPreview(ImDrawList& draw_list) {
  // Its top edge: dragged, it sets the preview's height
  const ImVec2 x_window_padding = ImVec2(20.0f, 0.0f);
  const ImVec2 top_bar_pos = ImGui::GetCursorScreenPos() - x_window_padding;
  const ImVec2 top_bar_size =
      ImVec2(ImGui::GetContentRegionAvail().x, 2.0f) + x_window_padding * 2;
  const float hover_area = 8.0f;
  const bool resizable = preview_open_ && !popped_out_;
  const bool top_bar_hovered =
      resizable && ImGui::IsMouseHoveringRect(
                       top_bar_pos - ImVec2(0.0f, hover_area),
                       top_bar_pos + ImVec2(top_bar_size.x, hover_area));
  static bool top_bar_active_drag = false;
  if (top_bar_hovered && ImGui::IsMouseClicked(0))
    top_bar_active_drag = true;
  if (top_bar_hovered || top_bar_active_drag)
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
  const float button = ImGui::GetFrameHeight();
  if (top_bar_active_drag) {
    preview_height_ = (ImGui::GetWindowPos().y + ImGui::GetWindowSize().y) -
                      ImGui::GetMousePos().y - button - 8.0f;
    preview_height_ =
        std::clamp(preview_height_, 80.0f,
                   std::max(80.0f, ImGui::GetWindowSize().y * 0.7f));
    if (!ImGui::IsMouseDown(0))
      top_bar_active_drag = false;
  }
  ImU32 top_bar_color = IM_COL32(120, 120, 120, 255);
  if (top_bar_hovered)
    top_bar_color = IM_COL32(255, 255, 255, 255);
  if (top_bar_active_drag)
    top_bar_color = EditorColor::accent_deep;
  draw_list.AddRectFilled(top_bar_pos, top_bar_pos + top_bar_size,
                          top_bar_color, 6.0f);

  // The header: a click folds it; its buttons, and popping it out
  const ImVec2 header_pos = top_bar_pos + ImVec2(x_window_padding.x, 4.0f);
  const float width = ImGui::GetContentRegionAvail().x;
  ImGui::PushID("##preview_header");
  ImGui::SetCursorScreenPos(header_pos);
  ImGui::SetNextItemAllowOverlap();
  if (ImGui::InvisibleButton("##fold", ImVec2(width, button)))
    preview_open_ = !preview_open_;
  if (ImGui::IsItemHovered())
    draw_list.AddRectFilled(header_pos, header_pos + ImVec2(width, button),
                            EditorColor::hover_overlay,
                            EditorSizes::control_radius);
  IMComponents::Glyph(draw_list, header_pos, ImVec2(12.0f, button),
                      preview_open_ && !popped_out_ ? ICON_FA_CHEVRON_DOWN
                                                    : ICON_FA_CHEVRON_RIGHT,
                      EditorColor::chevron, EditorStyles::GetFonts().s);
  ImFont* font = EditorStyles::GetFonts().h4_bold;
  draw_list.AddText(font, font->FontSize,
                    header_pos + ImVec2(18.0f, (button - font->FontSize) * 0.5f),
                    EditorColor::text, "Preview");

  // Popped out, its buttons go with it
  const ImVec2 right = header_pos + ImVec2(width - button, 0.0f);
  ImGui::SetCursorScreenPos(right);
  if (IMComponents::ToolButton(ICON_FA_UP_RIGHT_FROM_SQUARE, popped_out_,
                               ImVec2(button, button)))
    popped_out_ = !popped_out_;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(popped_out_ ? "Back into the inspector"
                                  : "Open in its own window");
  if (!popped_out_)
    preview_.RenderButtons(*inspected_, right - ImVec2(2.0f, 0.0f), button);
  ImGui::PopID();

  if (!preview_open_ || popped_out_)
    return;
  ImGui::SetCursorScreenPos(ImVec2(top_bar_pos.x, header_pos.y + button + 4.0f));
  ImGui::PushID("##preview");
  preview_.RenderView(*inspected_, ImVec2(top_bar_size.x, preview_height_));
  ImGui::PopID();
}

void InspectorPanel::RenderViewer() {
  if (!GUIUtils::BeginFloatingWindow("Asset Viewer", &popped_out_)) {
    GUIUtils::EndFloatingWindow();
    return;
  }
  if (inspected_ && !inspected_->PreviewMeshes().empty()) {
    const float button = ImGui::GetFrameHeight();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    viewer_.RenderButtons(
        *inspected_, at + ImVec2(ImGui::GetContentRegionAvail().x, 0.0f),
        button);
    ImGui::SetCursorScreenPos(at + ImVec2(0.0f, button + 4.0f));
    viewer_.RenderView(*inspected_, ImGui::GetContentRegionAvail());
  } else {
    ImGui::TextDisabled("Select something with meshes to view it here.");
  }
  GUIUtils::EndFloatingWindow();
}
