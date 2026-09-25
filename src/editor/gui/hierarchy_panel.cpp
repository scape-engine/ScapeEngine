#include "hierarchy_panel.h"

#include <algorithm>

#include "engine/transform/transform.h"

#include "editor/gui/inspectables/entity_inspectable.h"
#include "editor/gui/inspector_panel.h"
#include "editor/gui/utils/gui_utils.h"
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"

enum DropType { NO_DROP, DROP_ITEM, MOVE_ITEM_UP, MOVE_ITEM_DOWN };

//=============================================================================
// Hierarchy Panel
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

  // TODO: Setup drag rect here

  // Subscribe To Transform Creation/Destruction
  auto& reg = ECS::Main().Reg();

  on_create_connection_ = reg.on_construct<TransformComponent>()
                              .connect<&HierarchyPanel::OnEntityChanged>(this);
  on_destroy_connection_ = reg.on_destroy<TransformComponent>()
                               .connect<&HierarchyPanel::OnEntityChanged>(this);
}

void HierarchyPanel::Render() {
  // UE5 Outliners Have Zero Window Padding So Rows Span Edge-To-Edge
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::Begin(ICON_FA_SITEMAP " Hierarchy");
  {
    RenderPopupMenu();

    // Get Draw List
    ImDrawList& draw_list = *ImGui::GetWindowDrawList();

    RenderSearch(draw_list);

    // Add Small Padding Before The Actual Hierarchy List Starts
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

    RenderHierarchy(draw_list);
    RenderDraggedItem();

    PerformAutoScroll();
  }
  ImGui::End();
  ImGui::PopStyleVar();
}

void HierarchyPanel::RenderSearch(ImDrawList& draw_list) {
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 avail = ImGui::GetContentRegionAvail();
  float bar_height = 42.0f;

  // Use theme's darkest background (Void / TitleBg)
  ImU32 bar_bg = ImGui::GetColorU32(ImGuiCol_MenuBarBg);
  ImU32 border_col = ImGui::GetColorU32(ImGuiCol_Border);

  draw_list.AddRectFilled(pos, ImVec2(pos.x + avail.x, pos.y + bar_height),
                          bar_bg);
  draw_list.AddLine(ImVec2(pos.x, pos.y + bar_height),
                    ImVec2(pos.x + avail.x, pos.y + bar_height), border_col,
                    2.0f);

  // Store the exact starting Y position so both elements anchor perfectly
  float start_y = ImGui::GetCursorPosY() + 8.0f;
  ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 8.0f, start_y));

  // Use Theme Accent for the "+ Add" button
  ImGui::PushStyleColor(ImGuiCol_Button,
                        ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

  if (ImGui::Button(ICON_FA_PLUS " Add", ImVec2(60, 26.0f))) {
    ImGui::OpenPopup("HierarchyContextMenu");
  }

  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  // 8px horizontal gap between button and search bar
  ImGui::SameLine(0, 8.0f);

  // Force the search bar to anchor to the exact same top Y coordinate
  ImGui::SetCursorPosY(start_y);

  // Search Bar
  ImGui::PushFont(EditorStyles::GetFonts().p);

  // MATHEMATICAL ALIGNMENT:
  // Force the InputText to be exactly 26px tall so it perfectly matches the
  // button
  float font_size = ImGui::GetFontSize();
  float padding_y = (26.0f - font_size) * 0.5f;

  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, padding_y));
  ImGui::PushStyleColor(
      ImGuiCol_FrameBg,
      ImGui::GetStyle().Colors[ImGuiCol_WindowBg]);  // Match primary bg

  ImGui::PushItemWidth(avail.x - 60 - 24.0f);

  if (ImGui::InputTextWithHint(
          "##Search", ICON_FA_MAGNIFYING_GLASS " Search...", search_buffer,
          IM_ARRAYSIZE(search_buffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
    // TODO: Implement search filtering
  }

  ImGui::PopItemWidth();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);
  ImGui::PopFont();

  // Reset the cursor so the rest of the hierarchy draws below the bar
  ImGui::SetCursorPosY(start_y + 26.0f + 8.0f);
}

void HierarchyPanel::RenderHierarchy(ImDrawList& draw_list) {
  // Rebuild Hierarchy If Dirty
  if (hierarchy_dirty_) {
    BuildSceneHierarchy();
    hierarchy_dirty_ = false;
  }

  // Push Font (UE5 Uses Standard Fonts For Lists)
  ImGui::PushFont(EditorStyles::GetFonts().p);

  // Render Hierarchy
  for (auto& item : current_hierarchy) {
    RenderItem(draw_list, item, 0);
  }

  // Update Camera Movement
  UpdateCameraMovement();

  // Pop Font
  ImGui::PopFont();
}

void HierarchyPanel::RenderItem(ImDrawList& draw_list, HierarchyItem& item,
                                uint32_t indentation) {
  // Get Foreground Draw List
  ImDrawList* foreground_draw_list = ImGui::GetForegroundDrawList();

  // Properties
  const float indentation_offset = 16.0f;  // UE5 uses tighter indentation
  const float item_height = 22.0f;         // Dense row height

  // Evaluate Data
  ImGuiIO& io = ImGui::GetIO();
  uint32_t item_id = item.entity.Id();

  const bool selected = selected_items.count(item_id) > 0;
  const bool has_children = item.children.size() > 0;

  // Capture Cursor Position For Full-Width Backgrounds
  const ImVec2 window_pos = ImGui::GetWindowPos();
  const ImVec2 cursor_position = ImGui::GetCursorScreenPos();
  const ImVec2 content_region = ImGui::GetContentRegionAvail();
  const ImVec2 mouse_position = ImGui::GetMousePos();

  // Highlight Rectangle Spans The Entire Width Of The Window
  const ImVec2 rect_min = ImVec2(window_pos.x, cursor_position.y);
  const ImVec2 rect_max = ImVec2(window_pos.x + ImGui::GetWindowWidth(),
                                 cursor_position.y + item_height);
  const ImVec2 final_size = rect_max - rect_min;

  const bool hovered =
      ImGui::IsMouseHoveringRect(rect_min, rect_max) && !popup_menu_used;
  const bool clicked = hovered && ImGui::IsMouseClicked(0);
  const bool double_clicked = hovered && ImGui::IsMouseDoubleClicked(0);
  const bool dragging_this = hovered && ImGui::IsMouseDragging(0);

  if (hovered)
    last_hovered = &item;

  // Check For Drop Type
  DropType drop_type = NO_DROP;
  if (hovered && dragging_hierarchy) {
    if (mouse_position.y < rect_min.y + final_size.y * 0.25f)
      drop_type = MOVE_ITEM_UP;
    else if (mouse_position.y > rect_max.y - final_size.y * 0.25f)
      drop_type = MOVE_ITEM_DOWN;
    else
      drop_type = DROP_ITEM;
  }

  // Check For Moving Item Display
  ImU32 move_line_color = ImGui::GetColorU32(ImGuiCol_HeaderActive);
  const float move_line_thickness = 2.0f;
  if (drop_type == MOVE_ITEM_UP) {
    foreground_draw_list->AddLine(rect_min, ImVec2(rect_max.x, rect_min.y),
                                  move_line_color, move_line_thickness);
  } else if (drop_type == MOVE_ITEM_DOWN) {
    foreground_draw_list->AddLine(ImVec2(rect_min.x, rect_max.y), rect_max,
                                  move_line_color, move_line_thickness);
  }

  // Evaluate Color
  ImU32 bg_color = IM_COL32(0, 0, 0, 0);

  ImU32 accent_col = ImGui::GetColorU32(ImGuiCol_HeaderActive);
  ImU32 hover_col = ImGui::GetColorU32(ImGuiCol_HeaderHovered);
  ImU32 unfocused_col = GUIUtils::Darken(accent_col, 0.4f);
  ImU32 drop_col = GUIUtils::Darken(accent_col, 0.2f);

  if (selected) {
    bg_color = GUIUtils::WindowFocused() ? accent_col : unfocused_col;
  } else if (drop_type == DROP_ITEM) {
    bg_color = drop_col;
  } else if (hovered) {
    bg_color = hover_col;
  }

  // Draw Item Background
  if (bg_color != IM_COL32(0, 0, 0, 0)) {
    draw_list.AddRectFilled(rect_min, rect_max, bg_color);
  }

  // Evaluate Content Positions
  float start_x = window_pos.x + 8.0f + (indentation * indentation_offset);
  float text_y =
      cursor_position.y + (item_height - ImGui::GetFontSize()) * 0.5f;
  ImVec2 caret_pos = ImVec2(start_x, text_y);

  // Draw Caret
  bool caret_hovered = false;

  if (has_children) {
    // Caret Hitbox
    ImVec2 caret_hitbox_min = ImVec2(caret_pos.x - 4.0f, rect_min.y);
    ImVec2 caret_hitbox_max = ImVec2(caret_pos.x + 16.0f, rect_max.y);

    caret_hovered =
        ImGui::IsMouseHoveringRect(caret_hitbox_min, caret_hitbox_max);
    bool caret_clicked = caret_hovered && ImGui::IsMouseClicked(0);

    if (caret_clicked) {
      item.expanded = !item.expanded;
    }

    const char* icon =
        item.expanded ? ICON_FA_CHEVRON_DOWN : ICON_FA_CHEVRON_RIGHT;
    ImU32 caret_color = caret_hovered ? IM_COL32(255, 255, 255, 255)
                                      : IM_COL32(180, 180, 180, 255);
    draw_list.AddText(caret_pos, caret_color, icon);
  }

  // Draw Entity Icon And Text
  ImVec2 icon_pos = ImVec2(caret_pos.x + 18.0f, text_y);
  draw_list.AddText(icon_pos, IM_COL32(180, 180, 180, 255), ICON_FA_CUBE);

  ImVec2 text_pos = ImVec2(icon_pos.x + 22.0f, text_y);
  ImU32 text_color = selected ? IM_COL32_WHITE : IM_COL32(220, 220, 220, 255);
  draw_list.AddText(text_pos, text_color, item.entity.Name().c_str());

  // Check For Selection
  if (clicked && !caret_hovered) {
    auto select = [this](HierarchyItem& _item) -> void {
      if (selected_items.find(_item.entity.Id()) != selected_items.end())
        return;
      selected_items[_item.entity.Id()] = &_item;
      Runtime::State().SelectEntity(_item.entity.Handle());
    };

    if (io.KeyCtrl) {
      if (selected_items.find(item_id) == selected_items.end())
        select(item);
      else
        selected_items.erase(item_id);
    } else if (io.KeyShift) {
      if (!last_selected)
        last_selected = &item;
      auto start = std::find(current_hierarchy.begin(), current_hierarchy.end(),
                             *last_selected);
      auto end =
          std::find(current_hierarchy.begin(), current_hierarchy.end(), item);
      if (start != current_hierarchy.end() && end != current_hierarchy.end()) {
        if (start <= end) {
          for (auto i = start; i != end; ++i)
            select(*i);
          select(*end);
        } else {
          for (auto i = start; i != end; --i)
            select(*i);
          select(*end);
        }
      }
    } else {
      if (selected_items.size() > 1) {
        if (selected_items.find(item_id) == selected_items.end()) {
          selected_items.clear();
          select(item);
        }
      } else {
        selected_items.clear();
        select(item);
      }
    }
    last_selected = &item;
  }

  // Double Click / Focus
  if (double_clicked ||
      (selected && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)))
    SetCameraTarget(&item.entity.Transform());

  // Advance Cursor (Invisible Button To Capture Drags Safely)
  ImGui::SetCursorScreenPos(rect_min);
  ImGui::InvisibleButton(("##row_" + std::to_string(item_id)).c_str(),
                         ImVec2(content_region.x, item_height));

  // Drag And Drop Logic
  if (dragging_this && !dragging_hierarchy) {
    dragging_hierarchy = true;
  }

  if (dragging_hierarchy && !ImGui::IsMouseDown(0) && drop_type != NO_DROP) {
    switch (drop_type) {
      case DROP_ITEM:
        // TODO: Handle parent drop
        break;
      case MOVE_ITEM_UP:
        // TODO: Handle ordering
        break;
      case MOVE_ITEM_DOWN:
        // TODO: Handle ordering
        break;
    }
  }

  // Render Children
  if (has_children && item.expanded) {
    for (auto& child : item.children) {
      RenderItem(draw_list, child, indentation + 1);
    }
  }
}

void HierarchyPanel::RenderDraggedItem() {
  // Don't Proceed If No Item Is Being Dragged
  if (!dragging_hierarchy)
    return;

  // If Not Dragging Anymore, Stop
  if (!ImGui::IsMouseDown(0)) {
    dragging_hierarchy = false;
    return;
  }

  // UE5 Drag Preview Pill
  ImDrawList* fg = ImGui::GetForegroundDrawList();
  ImVec2 pos = ImGui::GetMousePos() + ImVec2(16.0f, 16.0f);

  size_t n_selected = selected_items.size();
  std::string text =
      n_selected > 1 ? std::to_string(n_selected) + " items"
                     : (last_selected ? last_selected->entity.Name() : "Item");

  std::string label = std::string(ICON_FA_LAYER_GROUP) + "  " + text;

  ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
  ImVec2 padding = ImVec2(12.0f, 6.0f);
  ImVec2 rect_min = pos;
  ImVec2 rect_max = pos + text_size + padding * 2;

  // Use Background Color And Darker Border For Drag Element
  ImU32 bg_col = ImGui::GetColorU32(ImGuiCol_MenuBarBg);
  fg->AddRectFilled(rect_min, rect_max, bg_col, 16.0f);
  fg->AddRect(rect_min, rect_max, IM_COL32(100, 100, 100, 200), 16.0f);
  fg->AddText(pos + padding, IM_COL32(255, 255, 255, 255), label.c_str());
}

void HierarchyPanel::RenderPopupMenu() {
  if (PopupMenu::Begin("HierarchyContextMenu")) {
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
      PopupMenu::ItemLight("Cube");
      PopupMenu::ItemLight("Sphere");
      PopupMenu::ItemLight("Capsule");
      PopupMenu::ItemLight("Cylinder");
      PopupMenu::ItemLight("Pyramid");
      PopupMenu::ItemLight("Plane");
      PopupMenu::EndMenu();
    }

    if (PopupMenu::Menu(ICON_FA_LIGHTBULB, "Light")) {
      PopupMenu::ItemLight("Directional Light");
      PopupMenu::ItemLight("Point Light");
      PopupMenu::ItemLight("Spotlight");
      PopupMenu::EndMenu();
    }

    if (PopupMenu::Menu(ICON_FA_VOLUME_HIGH, "Audio")) {
      PopupMenu::ItemLight("Audio Source");
      PopupMenu::EndMenu();
    }

    if (PopupMenu::Menu(ICON_FA_DIAGRAM_PROJECT, "PCG")) {
      if (PopupMenu::ItemLight("PCG Graph")) {
        auto& world = ECS::Main();
        auto [entity, transform] = world.CreateEntity("PCG Graph");

        world.Add<VolumeComponent>(entity);
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
  // Clear Current Hierarchy Before Rebuilding
  current_hierarchy.clear();

  // Get All Transforms
  auto transforms = ECS::Main().View<TransformComponent>();
  std::vector<std::pair<entt::entity, TransformComponent*>> transform_list;

  // Fill Transform List For Reversed Iteration
  for (auto [entity, transform] : transforms.each()) {
    transform_list.push_back({entity, &transform});
  }

  // Recursively Build Root Entities In Reverse
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

  // Get God Camera Transform
  TransformComponent& camera_transform =
      std::get<0>(Runtime::GetSceneViewPipeline().GetGodCamera());

  // Get Target Transform
  TransformComponent& target_transform = *camera_target;

  // Get Targets
  float distance = 5.0f + Transform::GetScale(target_transform, Space::WORLD).z;
  glm::vec3 target_position =
      Transform::GetPosition(target_transform, Space::WORLD) +
      glm::vec3(0.0f, 0.0f, -1.0f) * distance;

  float duration = 0.5f;
  if (camera_movement_time < duration) {
    // Calculate Position Delta
    float t = glm::clamp(camera_movement_time / duration, 0.0f, 1.0f);

    // Get Smoother Targets
    glm::vec3 new_position =
        glm::mix(camera_transform.position_, target_position, t);
    // TODO: Get rotation targets

    // Apply New Position
    camera_transform.position_ = new_position;
    // TODO: Apply new rotation

    // Add To Elapsed Camera Movement Time
    camera_movement_time += Time::Deltaf();
  } else {
    // Stop Camera Movement
    camera_transform.position_ = target_position;
    // TODO: Stop rotation

    // Reset
    camera_moving = false;
    camera_movement_time = 0.0f;
  }
}

void HierarchyPanel::PerformAutoScroll() {
  // No Item Dragged -> No Auto Scroll
  if (!dragging_hierarchy)
    return;

  // Properties
  const float max_scroll_speed = 35.0f;
  const float scroll_area = 0.15f;

  // Get Data
  float mouse_y = ImGui::GetMousePos().y;
  float window_y = ImGui::GetWindowPos().y;
  float window_height = ImGui::GetWindowHeight();

  auto range_factor = [](float x, const float range[2]) -> float {
    return 1.0f -
           glm::clamp((x - range[0]) / (range[1] - range[0]), 0.0f, 1.0f);
  };

  // Scroll Up
  float up_range[2] = {window_y, window_y + window_height * scroll_area};
  if (mouse_y < up_range[1]) {
    float scroll_speed = max_scroll_speed * range_factor(mouse_y, up_range);
    ImGui::SetScrollY(ImGui::GetScrollY() - scroll_speed);
    return;
  }

  // Scroll Down
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