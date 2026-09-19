#include "inspector_panel.h"

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
  // ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 20.0f));
  ImGui::Begin("Inspector", nullptr, EditorFlag::fixed);
  {
    ImDrawList& draw_list = *ImGui::GetWindowDrawList();

    IMComponents::Headline("Inspector", ICON_FA_LAYER_GROUP);

    // Drive inspector from EditorState selection
    auto& state = Runtime::State();
    auto& world = ECS::Main();
    Entity selection = state.selected_entity;

    if (selection != entt::null && world.Reg().valid(selection)) {
      // Only re-inspect if selection changed
      if (selection != last_inspected_entity_) {
        last_inspected_entity_ = selection;

        // Keep the HierarchyItem alive
        HierarchyItem item{EntityContainer(selection)};
        Inspect<EntityInspectable>(item);
      }
    } else {
      if (last_inspected_entity_ != entt::null) {
        last_inspected_entity_ = entt::null;
        inspected_ = nullptr;
      }
    }

    // Render inspected static content if available
    if (inspected_) {
      inspected_->RenderStaticContent(draw_list);
    }

    // Rendering preview
    bool rendering_preview = false;  // TODO: enable when preview pipeline ready

    ImGui::Dummy(ImVec2(0.0f, 2.0f));

    // Get sizes of content and preview viewer
    ImVec2 content_size = ImGui::GetContentRegionAvail();
    ImVec2 preview_size = ImVec2(0.0f, 0.0f);

    // Adjust size if preview viewer is being rendered
    if (rendering_preview) {
      content_size.y -= preview_viewer_height_ + EditorSizes::window_padding;
    }

    // Render inspected content if available
    if (inspected_) {

      // Inspect content child
      IMComponents::BeginClippedChild("##InspectorContent", content_size);
      { inspected_->RenderDynamicContent(draw_list); }
      IMComponents::EndClippedChild();

      // If available, render preview viewer
      if (rendering_preview)
        RenderPreviewViewer(draw_list, preview_size);

    } else {
      RenderNoneInspected();
    }
  }
  ImGui::End();
}

void InspectorPanel::RenderNoneInspected() {
  IMComponents::Label("Nothing selected", EditorStyles::GetFonts().h3_bold);
  IMComponents::Label("Select an entity or asset to inspect and edit it.",
                      EditorStyles::GetFonts().h4,
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
