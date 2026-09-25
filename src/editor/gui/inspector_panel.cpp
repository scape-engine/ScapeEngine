#include "inspector_panel.h"
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"
#include "editor/gui/hierarchy_panel.h"
#include "editor/gui/inspectables/entity_inspectable.h"
#include "editor/runtime/runtime.h"

InspectorPanel::InspectorPanel()
    : preview_viewer_output_(0),
      preview_viewer_height_(300.0f),
      preview_camera_transform_() {
  preview_viewer_output_ = Runtime::GetPreviewPipeline().CreateOutput();
}

void InspectorPanel::Render() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 20.0f));

  // Added Icon to match DockBuilder exactly
  ImGui::Begin(ICON_FA_LIST " Inspector", nullptr, ImGuiWindowFlags_NoCollapse);
  {
    ImDrawList& draw_list = *ImGui::GetWindowDrawList();

    IMComponents::Headline("Inspector", ICON_FA_LAYER_GROUP);

    auto& state = Runtime::State();
    auto& world = ECS::Main();
    Entity selection = state.selected_entity;

    if (selection != entt::null && world.Reg().valid(selection)) {
      if (selection != last_inspected_entity_) {
        last_inspected_entity_ = selection;
        HierarchyItem item{EntityContainer(selection)};
        Inspect<EntityInspectable>(item);
      }
    } else {
      if (last_inspected_entity_ != entt::null) {
        last_inspected_entity_ = entt::null;
        inspected_ = nullptr;
      }
    }

    if (inspected_) {
      inspected_->RenderStaticContent(draw_list);
    }

    bool rendering_preview = false;  // TODO: enable when preview pipeline ready

    ImVec2 content_size = ImGui::GetContentRegionAvail();
    ImVec2 preview_size = ImVec2(0.0f, 0.0f);

    if (rendering_preview) {
      preview_size =
          ImVec2(ImGui::GetContentRegionAvail().x, preview_viewer_height_);
      content_size.y -= preview_size.y;
      preview_size -= ImVec2(0.0f, -EditorSizes::window_padding);
    }

    if (inspected_) {
      ImVec2 margin = ImVec2(0.0f, 2.0f);
      ImGui::Dummy(margin);

      IMComponents::BeginClippedChild(content_size - margin);
      { inspected_->RenderDynamicContent(draw_list); }
      IMComponents::EndClippedChild();

      if (rendering_preview)
        RenderPreviewViewer(draw_list, preview_size);

    } else {
      RenderNoneInspected();
    }
  }
  ImGui::End();
  ImGui::PopStyleVar();
}

void InspectorPanel::RenderNoneInspected() {
  // UX Improvement: Center the empty state vertically & horizontally with an
  // icon
  ImVec2 avail = ImGui::GetContentRegionAvail();
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                       (avail.y * 0.35f));  // Push down 35%

  // Icon
  ImGui::PushFont(EditorStyles::GetFonts().h1);
  float icon_width = ImGui::CalcTextSize(ICON_FA_BOX_OPEN).x;
  ImGui::SetCursorPosX((ImGui::GetWindowWidth() - icon_width) * 0.5f);
  ImGui::TextDisabled(ICON_FA_BOX_OPEN);
  ImGui::PopFont();

  // Title
  float title_width = ImGui::CalcTextSize("Nothing selected").x;
  ImGui::SetCursorPosX((ImGui::GetWindowWidth() - title_width) * 0.5f);
  IMComponents::Label("Nothing selected", EditorStyles::GetFonts().h3_bold);

  // Subtitle
  const char* sub = "Select an entity or asset to inspect and edit it.";
  float sub_width = ImGui::CalcTextSize(sub).x;
  ImGui::SetCursorPosX((ImGui::GetWindowWidth() - sub_width) * 0.5f);
  IMComponents::Label(sub, EditorStyles::GetFonts().h4,
                      IM_COL32(210, 210, 255, 255));
}

void InspectorPanel::RenderPreviewViewer(ImDrawList& draw_list, ImVec2 size) {

  //
  // CREATE TOP BAR
  //
  const ImVec2 x_window_padding = ImVec2(20.0f, 0.0f);
  ImVec2 top_bar_pos = ImGui::GetCursorScreenPos() - x_window_padding;
  ImVec2 top_bar_size =
      ImVec2(ImGui::GetContentRegionAvail().x, 2.0f) + x_window_padding * 2;

  // Evaluate top bar interactions
  float hover_area = 8.0f;
  bool top_bar_hovered = ImGui::IsMouseHoveringRect(
      top_bar_pos - ImVec2(0.0f, hover_area),
      top_bar_pos + ImVec2(top_bar_size.x, hover_area));
  bool top_bar_interacted = top_bar_hovered && ImGui::IsMouseClicked(0);

  // TODO: set vertical resize cursor
  // if (top_bar_hovered) EditorUI::SetCursorType(CursorType::RESIZE_VERTICAL);

  // Start top bar drag
  static bool top_bar_active_drag = false;
  if (top_bar_interacted && !top_bar_active_drag)
    top_bar_active_drag = true;

  // Handle drag
  if (top_bar_active_drag) {
    preview_viewer_height_ =
        (ImGui::GetWindowPos().y + ImGui::GetWindowSize().y) -
        ImGui::GetMousePos().y;

    preview_viewer_height_ =
        std::clamp(preview_viewer_height_, 50.0f,
                   std::max(50.0f, ImGui::GetWindowSize().y * 0.5f));

    if (!ImGui::IsMouseDown(0))
      top_bar_active_drag = false;
  }

  // Evaluate top bar color
  ImU32 top_bar_color = IM_COL32(120, 120, 120, 255);
  if (top_bar_hovered)
    top_bar_color = IM_COL32(255, 255, 255, 255);
  if (top_bar_active_drag)
    top_bar_color = EditorColor::selection;

  // Draw top bar
  draw_list.AddRectFilled(top_bar_pos, top_bar_pos + top_bar_size,
                          top_bar_color, 6.0f);

  //
  // HANDLE PREVIEW OUTPUT
  //

  PreviewPipeline& pipeline = Runtime::GetPreviewPipeline();
  const PreviewOutput& output = pipeline.GetOutput(preview_viewer_output_);
  // TODO: handle output size, resize and render instructions

  //
  // DRAW RENDERED PREVIEW
  // ! this is a placeholder
  ImVec2 image_pos = top_bar_pos + ImVec2(0.0f, top_bar_size.y);
  ImVec2 image_size =
      size - ImVec2(0.0f, top_bar_size.y) + x_window_padding * 2;
  draw_list.AddRectFilled(image_pos, image_pos + image_size,
                          IM_COL32(30, 30, 30, 255));
}
