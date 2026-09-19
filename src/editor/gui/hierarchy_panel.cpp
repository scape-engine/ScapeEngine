#include "hierarchy_panel.h"

#include <algorithm>

#include "engine/transform/transform.h"

#include "editor/gui/inspectables/entity_inspectable.h"
#include "editor/gui/inspector_panel.h"

enum DropType { NO_DROP, DROP_ITEM, MOVE_ITEM_UP, MOVE_ITEM_DOWN };

//=============================================================================
// HIERARCHY PANEL
// includes:
//=============================================================================
HierarchyPanel::HierarchyPanel()
    : search_buffer(""),
      popup_menu_used(false),
      current_hierarchy(),
      selected_items(),
      last_selected(nullptr),
      last_hovered(nullptr),
      dragging_hierarchy(false),
      camera_moving(false),
      camera_movement_time(0.0f),
      camera_target(nullptr),
      hierarchy_dirty_(true) {

  /* TODO: setup drag rect here */

  // Subscribe to transform creation/destruction
  auto& reg = ECS::Main().Reg();

  on_create_connection_ = reg.on_construct<TransformComponent>()
                              .connect<&HierarchyPanel::OnEntityChanged>(this);
  on_destroy_connection_ = reg.on_destroy<TransformComponent>()
                               .connect<&HierarchyPanel::OnEntityChanged>(this);
}

void HierarchyPanel::Render() {
  // ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 20.0f));
  ImGui::Begin("Hierarchy", nullptr, EditorFlag::fixed);
  {
    RenderPopupMenu();

    // Get draw list
    ImDrawList& draw_list = *ImGui::GetWindowDrawList();

    // Header
    IMComponents::Headline("Hierarchy", ICON_FA_SITEMAP);

    RenderSearch(draw_list);
    RenderHierarchy(draw_list);
    RenderDraggedItem();

    PerformAutoScroll();
  }
  ImGui::End();
}

void HierarchyPanel::RenderSearch(ImDrawList& draw_list) {

  ImGui::PushFont(EditorStyles::GetFonts().h4);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 8.0f));
  ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

  // TODO: Implement search filtering
  if (ImGui::InputTextWithHint(
          "##Search", "Search...", search_buffer, IM_ARRAYSIZE(search_buffer),
          ImGuiInputTextFlags_EnterReturnsTrue)) { /* Implement search
                                                      filtering */
  }

  // TODO: adjust button position and make it work
  if (ImGui::Button(ICON_FA_PLUS)) {
    // TODO: button should display RenderPopupMenu when clicked
  }

  ImGui::PopFont();
  ImGui::PopItemWidth();
  ImGui::PopStyleVar();
  ImGui::Dummy(ImVec2(0.0f, 5.0f));
}

void HierarchyPanel::RenderHierarchy(ImDrawList& draw_list) {

  // Rebuild hierarchy if dirty
  if (hierarchy_dirty_) {
    BuildSceneHierarchy();
    hierarchy_dirty_ = false;
  }

  // Push font
  ImGui::PushFont(EditorStyles::GetFonts().p_bold);

  // Render hierarchy
  for (auto& item : current_hierarchy) {
    RenderItem(draw_list, item, 0);
  }

  // Update camera movement
  UpdateCameraMovement();

  // Pop font
  ImGui::PopFont();
}

void HierarchyPanel::RenderItem(ImDrawList& draw_list, HierarchyItem& item,
                                uint32_t indentation) {
  // GET FOREGROUND DRAW LIST
  ImDrawList* foreground_draw_list = ImGui::GetForegroundDrawList();

  // PROPERTIES
  const float indentation_offset = 30.0f;
  const ImVec2 text_padding = ImVec2(10.0f, 4.0f);

  // EVALUATE
  ImGuiIO& io = ImGui::GetIO();

  uint32_t item_id = item.entity.Id();

  const bool selected = selected_items.count(item_id) > 0;
  const bool has_children = item.children.size() > 0;
  const float item_height = ImGui::GetFontSize();
  const float text_offset = indentation * indentation_offset;

  const ImVec2 cursor_position = ImGui::GetCursorScreenPos();
  const ImVec2 content_region = ImGui::GetContentRegionAvail();
  const ImVec2 mouse_position = ImGui::GetMousePos();

  const ImVec2 rect_min = ImVec2(cursor_position.x, cursor_position.y);
  const ImVec2 rect_max =
      ImVec2(cursor_position.x + content_region.x,
             cursor_position.y + item_height + text_padding.y * 2);
  const ImVec2 final_size = rect_max - rect_min;

  const bool hovered =
      ImGui::IsMouseHoveringRect(rect_min, rect_max) && !popup_menu_used;
  const bool clicked = hovered && ImGui::IsMouseClicked(0);
  const bool double_clicked = hovered && ImGui::IsMouseDoubleClicked(0);
  const bool wheel_clicked = hovered && ImGui::IsMouseClicked(2);
  const bool dragging_this = hovered && ImGui::IsMouseDragging(0);

  if (hovered)
    last_hovered = &item;

  // CHECK FOR DROP TYPE ON THIS ITEM IF SOME ITEM IS CURRENTLY BEING DRAGGED
  DropType drop_type = NO_DROP;
  if (hovered && dragging_hierarchy) {
    // Mouse in the top quarter of item element (-> move item up)
    if (mouse_position.y < rect_min.y + final_size.y * 0.25f) {
      drop_type = MOVE_ITEM_UP;
    }
    // Mouse in the bottom quarter of item element (-> move item down)
    else if (mouse_position.y > rect_max.y - final_size.y * 0.25f) {
      drop_type = MOVE_ITEM_DOWN;
    }
    // Mouse in the middle of item element (-> drop item)
    else {
      drop_type = DROP_ITEM;
    }
  }

  // CHECK FOR MOVING ITEM DISPLAY
  ImU32 move_line_color = EditorColor::selection;
  const float move_line_thickness = 1.0f;
  const float move_line_offset = 2.0f;
  switch (drop_type) {
    case MOVE_ITEM_UP:
      foreground_draw_list->AddLine(
          ImVec2(rect_min.x, rect_min.y - move_line_offset),
          ImVec2(rect_max.x, rect_min.y - move_line_offset), move_line_color,
          move_line_thickness);
      break;
    case MOVE_ITEM_DOWN:
      foreground_draw_list->AddLine(
          ImVec2(rect_min.x, rect_max.y + move_line_offset),
          ImVec2(rect_max.x, rect_max.y + move_line_offset), move_line_color,
          move_line_thickness);
      break;
  }

  // EVALUATE COLOR

  // Base: Standard background color
  ImU32 color = EditorColor::background;
  // Priority #3: Color when item is hovered
  if (hovered)
    color = GUIUtils::Lighten(EditorColor::background, 0.38f);
  // Priority #2: Color when item is selected
  if (selected)
    color = GUIUtils::WindowFocused() ? EditorColor::selection
                                      : EditorColor::selection_inactive;
  // Priority #1: Color when item is being dropped
  if (drop_type == DROP_ITEM)
    color = GUIUtils::Darken(EditorColor::selection, 0.5f);

  // DRAW ITEM BACKGROUND
  draw_list.AddRectFilled(rect_min, rect_max, color, 5.0f);

  // CHECK FOR SELECTION
  if (clicked) {
    auto select = [this](HierarchyItem& _item) -> void {
      // Item already selected
      if (selected_items.find(_item.entity.Id()) != selected_items.end())
        return;

      // Select item
      selected_items[_item.entity.Id()] = &_item;

      // Update selection
      Runtime::State().SelectEntity(_item.entity.Handle());

      // InspectorPanel::Inspect<EntityInspectable>(_item);
    };

    // Handle current items selection
    if (io.KeyCtrl) {
      // Item not selected yet, add to selected items
      if (selected_items.find(item_id) == selected_items.end()) {
        select(item);
      }
      // Item already selected, remove from selected items
      else {
        selected_items.erase(item_id);
      }
    }
    // Select all between latest selection and this item
    else if (io.KeyShift) {
      // Make sure last selected is set to prevent memory errors
      if (!last_selected)
        last_selected = &item;

      // Find multiselect start and end elements
      auto start = std::find(current_hierarchy.begin(), current_hierarchy.end(),
                             *last_selected);
      auto end =
          std::find(current_hierarchy.begin(), current_hierarchy.end(), item);

      // Select all items between start and end
      if (start != current_hierarchy.end() && end != current_hierarchy.end()) {
        if (start <= end) {
          for (auto i = start; i != end; ++i) {
            select(*i);
          }
          select(*end);
        } else {
          for (auto i = start; i != end; --i) {
            select(*i);
          }
          select(*end);
        }
      }
    }

    // Only select this item
    else {
      // If there's multiple selected items, only select this item if it's not
      // among the multiple selected ones
      if (selected_items.size() > 1) {
        if (selected_items.find(item_id) == selected_items.end()) {
          selected_items.clear();
          select(item);
        }
      }
      // Not multiple selected items, just select this item
      else {
        selected_items.clear();
        select(item);
      }
    }

    // Cache last selected item
    last_selected = &item;
  }

  // CHECK FOR DOUBLE CLICK (-> move to entity)
  if (double_clicked)
    SetCameraTarget(&item.entity.Transform());

  // CHECK FOR CTRL F KEYPRESS WHEN SELECTED (-> move to entity)
  if (selected && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F))
    SetCameraTarget(&item.entity.Transform());

  // EVALUATE ITEM TEXT POSITION
  ImVec2 text_pos = ImVec2(rect_min.x + text_padding.x + text_offset,
                           rect_min.y + text_padding.y);

  // DRAW ITEM CARET CIRCLE
  if (has_children) {
    // Circle geometry
    float circle_radius = 9.0f;
    ImVec2 circle_position = ImVec2(text_pos.x + circle_radius + 1.5f,
                                    text_pos.y + circle_radius * 0.5f + 1.5f);

    // Fetch circle interactions
    float circle_distance = (mouse_position.x - circle_position.x) *
                                (mouse_position.x - circle_position.x) +
                            (mouse_position.y - circle_position.y) *
                                (mouse_position.y - circle_position.y);

    bool circle_hovered = circle_distance <= (circle_radius * circle_radius);
    bool circle_clicked = ImGui::IsMouseClicked(0) && circle_hovered;

    // Check for circle click or mouse wheel click (-> expand)
    if (circle_clicked || wheel_clicked)
      item.expanded = !item.expanded;

    // Evaluate color
    ImU32 circle_color = circle_hovered && drop_type == NO_DROP
                             ? (selected ? GUIUtils::Darken(color, 0.25f)
                                         : EditorColor::element_active)
                             : color;

    // Draw circle
    draw_list.AddCircleFilled(circle_position, circle_radius, circle_color);
  }

  // EVALUATE ICON
  const char* icon = has_children ? (item.expanded ? " " ICON_FA_CARET_DOWN
                                                   : " " ICON_FA_CARET_RIGHT)
                                  : "";

  // DRAW TEXT
  std::string text_value = std::string(icon) + "   " + item.entity.Name();
  draw_list.AddText(text_pos, EditorColor::text, text_value.c_str());

  // ADVANCE CURSOR
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
  ImGui::Dummy(ImVec2(content_region.x, final_size.y - 3.0f));
  ImGui::PopStyleVar();

  // CHECK FOR BEGINNING TO DRAG THIS ITEM
  if (dragging_this && !dragging_hierarchy) {
    dragging_hierarchy = true;
    // TODO: update drag rect action here
    // TODO: handle drag rect transition here
  }

  // CHECK FOR ENDING DRAG ON THIS ITEM (-> DROPPING HERE)
  if (dragging_hierarchy && !ImGui::IsMouseDown(0) && drop_type != NO_DROP) {
    switch (drop_type) {
      case DROP_ITEM:
        // TODO: Drop dragged item here action
        // for (auto [id, sel] : selected_items)
        //   ECS::Main().SetParent(sel->entity.Handle(), item.entity.Handle());
        break;
      case MOVE_ITEM_UP:
        // TODO: Moved item up action
        break;
      case MOVE_ITEM_DOWN:
        // TODO: Moved item down action
        break;
    }
  }

  // RENDER CHILDREN
  if (has_children && item.expanded) {
    for (auto& child : item.children) {
      RenderItem(draw_list, child, indentation + 1);
    }
  }
}

void HierarchyPanel::RenderDraggedItem() {
  // Don't proceed if no item is being dragged
  if (!dragging_hierarchy)
    return;

  // If not dragging anymore, stop
  if (!ImGui::IsMouseDown(0)) {
    dragging_hierarchy = false;
    return;
  }

  // TODO: draw drag rect here, replace code below

  // ! PLACEHOLDER: Draw drag indicator at mouse position
  ImDrawList* fg = ImGui::GetForegroundDrawList();
  ImVec2 pos = ImGui::GetMousePos() + ImVec2(12.0f, 12.0f);

  size_t n_selected = selected_items.size();
  std::string text =
      n_selected > 1 ? std::to_string(n_selected) + " selected"
                     : (last_selected ? "Moving " + last_selected->entity.Name()
                                      : "Moving");

  std::string label = std::string(ICON_FA_LEFT_LONG) + "   " + text;

  ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
  ImVec2 padding = ImVec2(20.0f, 10.0f);
  ImVec2 rect_min = pos;
  ImVec2 rect_max = pos + text_size + padding * 2;

  fg->AddRectFilled(rect_min, rect_max, EditorColor::selection, 5.0f);
  fg->AddText(pos + padding, IM_COL32(255, 255, 255, 255), label.c_str());
}

void HierarchyPanel::RenderPopupMenu() {
  if (PopupMenu::Begin()) {
    popup_menu_used = true;

    if (last_hovered) {
      if (PopupMenu::Item(ICON_FA_TRASH, "Delete")) {
        // TODO: Handle delete
      }

      if (PopupMenu::Item(ICON_FA_COPY, "Duplicate")) {
        // TODO: Handle duplicate
      }

      if (PopupMenu::Item(ICON_FA_PENCIL, "Rename")) {
        // TODO: Handle rename
      }

      PopupMenu::Separator();
    }

    if (PopupMenu::Item(ICON_FA_PLUS, "Empty Entity")) {
      // TODO: Handle empty entity
    }

    if (PopupMenu::Item(ICON_FA_CAMERA, "Camera")) {
      // TODO: Handle camera
    }

    if (PopupMenu::Menu(ICON_FA_CUBE, "3D Primitives")) {
      if (PopupMenu::ItemLight("Cube")) { /* TODO: Handle cube */
      }
      if (PopupMenu::ItemLight("Sphere")) { /* TODO: Handle sphere */
      }
      if (PopupMenu::ItemLight("Capsule")) { /* TODO: Handle capsule */
      }
      if (PopupMenu::ItemLight("Cylinder")) { /* TODO: Handle cylinder */
      }
      if (PopupMenu::ItemLight("Pyramid")) { /* TODO: Handle pyramid */
      }
      if (PopupMenu::ItemLight("Plane")) { /* TODO: Handle plane */
      }

      PopupMenu::EndMenu();
    }

    if (PopupMenu::Menu(ICON_FA_LIGHTBULB, "Light")) {
      if (PopupMenu::ItemLight("Directional Light")) { /* TODO */
      }
      if (PopupMenu::ItemLight("Point Light")) { /* TODO */
      }
      if (PopupMenu::ItemLight("Spotlight")) { /* TODO */
      }

      PopupMenu::EndMenu();
    }

    if (PopupMenu::Menu(ICON_FA_VOLUME_HIGH, "Audio")) {
      if (PopupMenu::ItemLight("Audio Source")) { /* TODO */
      }

      PopupMenu::EndMenu();
    }

    if (PopupMenu::Menu(ICON_FA_DIAGRAM_PROJECT, "PCG")) {
      if (PopupMenu::ItemLight("PCG Graph")) {
        auto& world = ECS::Main();
        auto [entity, transform] = world.CreateEntity("PCG Graph");

        world.Add<VolumeComponent>(entity);
        // ? MeshRendererComponent should be added later when generator produces
        // geometry
      }

      PopupMenu::EndMenu();
    }

    PopupMenu::End();
  } else {
    popup_menu_used = false;
    last_hovered = nullptr;
  }

  PopupMenu::Pop();
}

void HierarchyPanel::BuildSceneHierarchy() {
  // Clear current hierarchy before rebuilding
  current_hierarchy.clear();

  // Get all transforms
  auto transforms = ECS::Main().View<TransformComponent>();
  std::vector<std::pair<entt::entity, TransformComponent*>> transform_list;

  // Fill transform list for reversed iteration
  for (auto [entity, transform] : transforms.each()) {
    transform_list.push_back({entity, &transform});
  }

  // Recursively build root entities in reverse
  for (auto it = transform_list.rbegin(); it != transform_list.rend(); ++it) {
    auto& [entity, transform] = *it;
    if (Transform::HasParent(*transform))
      continue;
    HierarchyItem item = HierarchyItem(EntityContainer(entity));
    BuildHierarchyChildren(item);
    current_hierarchy.push_back(item);
  }
}

void HierarchyPanel::BuildHierarchyChildren(HierarchyItem& parent) {
  for (Entity child_entity : parent.entity.Transform().children_) {
    HierarchyItem item = HierarchyItem(EntityContainer(child_entity));
    BuildHierarchyChildren(item);
    parent.children.push_back(item);
  }
}

void HierarchyPanel::SetCameraTarget(TransformComponent* target) {
  camera_target = target;
  camera_moving = true;
  camera_movement_time = 0.0f;
}

void HierarchyPanel::UpdateCameraMovement() {
  if (!camera_moving || !camera_target)
    return;

  // Get god camera transform
  TransformComponent& camera_transform =
      std::get<0>(Runtime::GetSceneViewPipeline().GetGodCamera());

  // Get target transform
  TransformComponent& target_transform = *camera_target;

  // Get targets
  float distance = 5.0f + Transform::GetScale(target_transform, Space::WORLD).z;
  glm::vec3 target_position =
      Transform::GetPosition(target_transform, Space::WORLD) +
      glm::vec3(0.0f, 0.0f, -1.0f) * distance;

  float duration = 0.5f;
  if (camera_movement_time < duration) {
    // Calculate position delta
    float t = glm::clamp(camera_movement_time / duration, 0.0f, 1.0f);

    // Get smoother targets
    glm::vec3 new_position =
        glm::mix(camera_transform.position_, target_position, t);
    // TODO: get rotation targets

    // Apply new position
    camera_transform.position_ = new_position;
    // TODO: apply new rotation

    // Add to elapsed camera movement time
    camera_movement_time += Time::Deltaf();
  } else {
    // Stop camera movement
    camera_transform.position_ = target_position;
    // TODO: stop rotation

    // Reset
    camera_moving = false;
    camera_movement_time = 0.0f;
  }
}

void HierarchyPanel::PerformAutoScroll() {
  // No item dragged -> no auto scroll
  if (!dragging_hierarchy)
    return;

  // Properties
  const float max_scroll_speed = 35.0f;
  const float scroll_area = 0.15f;

  // Get data
  float mouse_y = ImGui::GetMousePos().y;
  float window_y = ImGui::GetWindowPos().y;
  float window_height = ImGui::GetWindowHeight();

  auto range_factor = [](float x, const float range[2]) -> float {
    return 1.0f -
           glm::clamp((x - range[0]) / (range[1] - range[0]), 0.0f, 1.0f);
  };

  // Scroll up
  float up_range[2] = {window_y, window_y + window_height * scroll_area};
  if (mouse_y < up_range[1]) {
    float scroll_speed = max_scroll_speed * range_factor(mouse_y, up_range);
    ImGui::SetScrollY(ImGui::GetScrollY() - scroll_speed);
    return;
  }

  // Scroll down
  float down_range[2] = {window_y + window_height,
                         window_y + window_height * (1.0f - scroll_area)};
  if (mouse_y > down_range[1]) {
    float scroll_speed = max_scroll_speed * range_factor(mouse_y, down_range);
    ImGui::SetScrollY(ImGui::GetScrollY() + scroll_speed);
  }
}

void HierarchyPanel::OnEntityChanged(entt::registry&, entt::entity) {
  hierarchy_dirty_ = true;
}