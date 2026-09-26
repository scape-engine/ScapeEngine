#include "hierarchy_panel.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "imgui_internal.h"

#include "engine/transform/transform.h"

#include "editor/gui/inspectables/entity_inspectable.h"
#include "editor/gui/inspector_panel.h"
#include "editor/gui/utils/gui_utils.h"

#include "engine/ecs/components/camera_component.h"
#include "engine/ecs/components/light_component.h"
#include "engine/ecs/components/mesh_renderer_component.h"

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

//=============================================================================
// RENDER
//=============================================================================
void HierarchyPanel::Render() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  GUIUtils::HideDockTabBar();
  ImGui::Begin("Hierarchy", nullptr, EditorFlag::fixed);
  ImGui::PopStyleVar();
  {
    RenderPopupMenu();
    ImDrawList& draw_list = *ImGui::GetWindowDrawList();
    RenderHeader(draw_list);
    RenderHierarchy(draw_list);
    RenderDraggedItem();
    PerformAutoScroll();
  }
  ImGui::End();
}

void HierarchyPanel::RenderSearch(ImDrawList& draw_list, ImVec2 position,
                                  ImVec2 size) {
  IMComponents::SearchField(draw_list, "##HierarchySearch", search_buffer,
                            IM_ARRAYSIZE(search_buffer), position, size);
}

bool HierarchyPanel::MatchesSearch(const HierarchyItem& item) const {
  if (!Searching())
    return true;

  auto lower = [](std::string s) {
    for (char& c : s)
      c = static_cast<char>(std::tolower(c));
    return s;
  };
  if (lower(item.entity.Name()).find(lower(search_buffer)) != std::string::npos)
    return true;

  for (const auto& child : item.children) {
    if (MatchesSearch(child))
      return true;
  }
  return false;
}

//=============================================================================
// HEADER
//=============================================================================
void HierarchyPanel::RenderHeader(ImDrawList& draw_list) {
  const ImVec2 strip_min = ImGui::GetCursorScreenPos();
  const float panel_width = ImGui::GetContentRegionAvail().x;
  const ImVec2 strip_max =
      ImVec2(strip_min.x + panel_width, strip_min.y + header_height_);
  const float y = strip_min.y + (header_height_ - search_height_) * 0.5f;

  const ImVec2 search_pos = ImVec2(strip_min.x + header_pad_x_, y);
  const ImVec2 search_size =
      ImVec2(panel_width * search_width_ratio_, search_height_);
  RenderSearch(draw_list, search_pos, search_size);

  RenderActions(draw_list,
                ImVec2(search_pos.x + search_size.x + header_gap_, y),
                search_height_);

  // Divider between header strip and tree
  draw_list.AddLine(ImVec2(strip_min.x, strip_max.y - 0.5f),
                    ImVec2(strip_max.x, strip_max.y - 0.5f),
                    EditorColor::panel_stroke, 1.0f);

  ImGui::SetCursorScreenPos(ImVec2(strip_min.x, strip_max.y + content_gap_));
}

void HierarchyPanel::RenderActions(ImDrawList& draw_list, ImVec2 position,
                                   float height) {
  if (IMComponents::IconDropdownButton(draw_list, "##HierarchyActions",
                                       "open-module", position, height)) {
    PopupMenu::Open();
  }
}

void HierarchyPanel::RenderHierarchy(ImDrawList& draw_list) {
  if (hierarchy_dirty_) {
    BuildSceneHierarchy();
    hierarchy_dirty_ = false;
  }

  ImGui::PushFont(EditorStyles::GetFonts().p);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

  RenderRootHeader(draw_list);

  if (root_expanded_) {
    const ImVec2 children_start = ImGui::GetCursorScreenPos();

    for (auto& item : current_hierarchy) {
      if (MatchesSearch(item))
        RenderItem(draw_list, item, 1);
    }

    // Vertical guide under the root caret
    const ImVec2 children_end = ImGui::GetCursorScreenPos();
    const float guide_x =
        std::floor(children_start.x + row_pad_x_ + caret_slot_ * 0.5f);
    draw_list.AddLine(ImVec2(guide_x, children_start.y),
                      ImVec2(guide_x, children_end.y), EditorColor::tree_guide,
                      1.0f);
  }

  UpdateCameraMovement();

  ImGui::PopStyleVar();
  ImGui::PopFont();
}

void HierarchyPanel::RenderRootHeader(ImDrawList& draw_list) {
  const ImVec2 rect_min = ImGui::GetCursorScreenPos();
  const ImVec2 rect_max = ImVec2(rect_min.x + ImGui::GetContentRegionAvail().x,
                                 rect_min.y + row_height_);

  const bool hovered =
      ImGui::IsMouseHoveringRect(rect_min, rect_max) && !popup_menu_used;
  if (hovered)
    draw_list.AddRectFilled(rect_min, rect_max, EditorColor::hover_overlay,
                            2.0f);
  if (hovered && ImGui::IsMouseClicked(0))
    root_expanded_ = !root_expanded_;

  const float x0 = rect_min.x + row_pad_x_;
  DrawChevron(draw_list, ImVec2(x0, rect_min.y), root_expanded_, true,
              EditorColor::text);
  DrawGlyph(draw_list, ImVec2(x0 + caret_slot_, rect_min.y),
            ImVec2(icon_slot_, row_height_), ICON_FA_BOX_ARCHIVE,
            EditorColor::text);
  const ImVec2 text_pos =
      ImVec2(x0 + caret_slot_ + icon_slot_ + text_gap_,
             rect_min.y + (row_height_ - ImGui::GetFontSize()) * 0.5f);
  draw_list.AddText(text_pos, EditorColor::text, "Scene Hierarchy");

  ImGui::Dummy(ImVec2(0.0f, row_height_));
}

void HierarchyPanel::RenderItem(ImDrawList& draw_list, HierarchyItem& item,
                                uint32_t indentation) {
  ImDrawList* foreground_draw_list = ImGui::GetForegroundDrawList();
  ImGuiIO& io = ImGui::GetIO();

  // EVALUATE
  const uint32_t item_id = item.entity.Id();
  const bool selected = selected_items.count(item_id) > 0;
  const bool has_children = item.children.size() > 0;
  const float x_offset = indentation * indent_;

  const ImVec2 cursor_position = ImGui::GetCursorScreenPos();
  const ImVec2 content_region = ImGui::GetContentRegionAvail();
  const ImVec2 mouse_position = ImGui::GetMousePos();

  const ImVec2 rect_min = cursor_position;
  const ImVec2 rect_max = ImVec2(cursor_position.x + content_region.x,
                                 cursor_position.y + row_height_);
  const ImVec2 final_size = rect_max - rect_min;

  const bool hovered =
      ImGui::IsMouseHoveringRect(rect_min, rect_max) && !popup_menu_used;
  const bool double_clicked = hovered && ImGui::IsMouseDoubleClicked(0);
  const bool wheel_clicked = hovered && ImGui::IsMouseClicked(2);
  const bool dragging_this = hovered && ImGui::IsMouseDragging(0);

  if (hovered)
    last_hovered = &item;

  // DROP TYPE (unchanged)
  DropType drop_type = NO_DROP;
  if (hovered && dragging_hierarchy) {
    if (mouse_position.y < rect_min.y + final_size.y * 0.25f)
      drop_type = MOVE_ITEM_UP;
    else if (mouse_position.y > rect_max.y - final_size.y * 0.25f)
      drop_type = MOVE_ITEM_DOWN;
    else
      drop_type = DROP_ITEM;
  }

  // MOVE LINES (unchanged, recolored)
  const ImU32 move_line_color = EditorColor::accent;
  const float move_line_thickness = 1.0f;
  const float move_line_offset = 2.0f;
  switch (drop_type) {
    case MOVE_ITEM_UP:
      foreground_draw_list->AddLine(
          ImVec2(rect_min.x + x_offset, rect_min.y - move_line_offset),
          ImVec2(rect_max.x, rect_min.y - move_line_offset), move_line_color,
          move_line_thickness);
      break;
    case MOVE_ITEM_DOWN:
      foreground_draw_list->AddLine(
          ImVec2(rect_min.x + x_offset, rect_max.y + move_line_offset),
          ImVec2(rect_max.x, rect_max.y + move_line_offset), move_line_color,
          move_line_thickness);
      break;
    default:
      break;
  }

  // ROW BACKGROUND
  ImU32 row_color = IM_COL32(0, 0, 0, 0);
  if (hovered)
    row_color = EditorColor::hover_overlay;
  if (selected)
    row_color = GUIUtils::WindowFocused() ? EditorColor::selection_row
                                          : EditorColor::control_selected;
  if (drop_type == DROP_ITEM)
    row_color = EditorColor::accent_deep;
  if (row_color != 0)
    draw_list.AddRectFilled(rect_min, rect_max, row_color, 2.0f);

  // FOREGROUND COLORS
  const ImU32 text_color =
      selected ? EditorColor::accent_border : EditorColor::text;
  const ImU32 chevron_color =
      selected ? EditorColor::accent_border : EditorColor::text;

  // RIGHT-ALIGNED ICONS (data / eye / camera) — drawn before hit-testing the
  // row so clicking a toggle doesn't also select
  const bool icon_clicked =
      RenderItemIcons(draw_list, item, rect_min, rect_max);

  // CHEVRON (only when there are children; keeps alignment otherwise)
  const ImVec2 caret_min =
      ImVec2(rect_min.x + row_pad_x_ + x_offset, rect_min.y);
  const bool chevron_clicked = DrawChevron(draw_list, caret_min, item.expanded,
                                           has_children, chevron_color);
  if (has_children && (chevron_clicked || wheel_clicked))
    item.expanded = !item.expanded;

  // TYPE ICON + NAME
  DrawGlyph(draw_list, ImVec2(caret_min.x + caret_slot_, rect_min.y),
            ImVec2(icon_slot_, row_height_), EntityIcon(item),
            EditorColor::accent_border);

  const ImVec2 text_pos =
      ImVec2(caret_min.x + caret_slot_ + icon_slot_ + text_gap_,
             rect_min.y + (row_height_ - ImGui::GetFontSize()) * 0.5f);
  draw_list.AddText(text_pos, text_color, item.entity.Name().c_str());

  // SELECTION (unchanged logic; ignores clicks consumed by chevron/icons)
  const bool clicked =
      hovered && ImGui::IsMouseClicked(0) && !chevron_clicked && !icon_clicked;
  if (clicked) {
    auto select = [this](HierarchyItem& _item) -> void {
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
        } else {
          for (auto i = start; i != end; --i)
            select(*i);
        }
        select(*end);
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

  // FOCUS CAMERA (unchanged)
  if (double_clicked)
    SetCameraTarget(&item.entity.Transform());
  if (selected && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F))
    SetCameraTarget(&item.entity.Transform());

  // ADVANCE CURSOR
  ImGui::Dummy(ImVec2(0.0f, row_height_));

  // START DRAG (unchanged)
  if (dragging_this && !dragging_hierarchy && selected)
    dragging_hierarchy = true;

  // DROP (unchanged TODOs)
  if (dragging_hierarchy && !ImGui::IsMouseDown(0) && drop_type != NO_DROP) {
    switch (drop_type) {
      case DROP_ITEM: /* TODO */
        break;
      case MOVE_ITEM_UP: /* TODO */
        break;
      case MOVE_ITEM_DOWN: /* TODO */
        break;
      default:
        break;
    }
  }

  // CHILDREN (auto-expanded while searching)
  if (has_children && (item.expanded || Searching())) {
    for (auto& child : item.children) {
      if (MatchesSearch(child))
        RenderItem(draw_list, child, indentation + 1);
    }
  }
}

bool HierarchyPanel::RenderItemIcons(ImDrawList& draw_list, HierarchyItem& item,
                                     const ImVec2& rect_min,
                                     const ImVec2& rect_max) {
  bool any_clicked = false;

  // Slots from the right edge: [data][eye][camera]
  auto slot_min = [&](int index_from_right) {
    return ImVec2(
        rect_max.x - row_pad_x_ - toggle_slot_ * (index_from_right + 1),
        rect_min.y);
  };
  auto slot_clicked = [&](ImVec2 min) {
    ImVec2 max = ImVec2(min.x + toggle_slot_, rect_max.y);
    return ImGui::IsMouseHoveringRect(min, max) && ImGui::IsMouseClicked(0);
  };

  // Camera (render visibility) — placeholder toggle
  const ImVec2 cam_min = slot_min(0);
  if (slot_clicked(cam_min)) {
    item.renderable = !item.renderable;
    any_clicked = true;
  }
  DrawGlyph(draw_list, cam_min, ImVec2(icon_slot_, row_height_), ICON_FA_CAMERA,
            item.renderable ? EditorColor::text : EditorColor::text_disabled);

  // Eye (viewport visibility) — placeholder toggle
  const ImVec2 eye_min = slot_min(1);
  if (slot_clicked(eye_min)) {
    item.visible = !item.visible;
    any_clicked = true;
  }
  DrawGlyph(draw_list, eye_min, ImVec2(icon_slot_, row_height_),
            item.visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH,
            item.visible ? EditorColor::text : EditorColor::text_disabled);

  // Data icon (component type, green) — non-interactive for now
  const char* data_icon = EntityDataIcon(item);
  if (data_icon)
    DrawGlyph(draw_list, slot_min(2), ImVec2(icon_slot_, row_height_),
              data_icon, EditorColor::success);

  return any_clicked;
}

void HierarchyPanel::DrawGlyph(ImDrawList& draw_list, ImVec2 slot_min,
                               ImVec2 slot_size, const char* glyph, ImU32 color,
                               ImFont* font) {
  if (!font)
    font = ImGui::GetFont();
  const ImVec2 size = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, glyph);

  // Center horizontally on the visible ink, not the advance width
  unsigned int codepoint = 0;
  ImTextCharFromUtf8(&codepoint, glyph, nullptr);
  const ImFontGlyph* g = font->FindGlyph((ImWchar)codepoint);
  const float ink_center = g ? (g->X0 + g->X1) * 0.5f : size.x * 0.5f;

  const ImVec2 pos = ImVec2(slot_min.x + slot_size.x * 0.5f - ink_center,
                            slot_min.y + (slot_size.y - size.y) * 0.5f);
  draw_list.AddText(font, font->FontSize, pos, color, glyph);
}

bool HierarchyPanel::DrawChevron(ImDrawList& draw_list, ImVec2 slot_min,
                                 bool expanded, bool interactive, ImU32 color) {
  const ImVec2 slot_max =
      ImVec2(slot_min.x + caret_slot_, slot_min.y + row_height_);
  const bool hovered =
      interactive && ImGui::IsMouseHoveringRect(slot_min, slot_max);
  DrawGlyph(
      draw_list, slot_min, ImVec2(caret_slot_, row_height_),
      expanded ? ICON_FA_CHEVRON_DOWN : ICON_FA_CHEVRON_RIGHT,
      hovered ? EditorColor::text_bright : color);  // default font (p), not s
  return hovered && ImGui::IsMouseClicked(0);
}

const char* HierarchyPanel::EntityIcon(const HierarchyItem& item) const {
  const EntityContainer& e = item.entity;
  if (e.Has<CameraComponent>())
    return ICON_FA_VIDEO;
  if (e.Has<PointLightComponent>() || e.Has<DirectionalLightComponent>() ||
      e.Has<SkyLightComponent>())
    return ICON_FA_LIGHTBULB;
  if (e.Has<MeshRendererComponent>())
    return ICON_FA_CUBE;
  return ICON_FA_CIRCLE_NODES;  // empty / transform-only entity
}

const char* HierarchyPanel::EntityDataIcon(const HierarchyItem& item) const {
  const EntityContainer& e = item.entity;
  if (e.Has<CameraComponent>())
    return ICON_FA_CAMERA_RETRO;
  if (e.Has<PointLightComponent>() || e.Has<DirectionalLightComponent>() ||
      e.Has<SkyLightComponent>())
    return ICON_FA_SUN;
  if (e.Has<MeshRendererComponent>())
    return ICON_FA_DRAW_POLYGON;
  return nullptr;
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