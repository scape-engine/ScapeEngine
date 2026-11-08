#include "hierarchy_panel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "imgui_internal.h"

#include "engine/renderer/icons/icon_loader.h"
#include "engine/scene/model_entity_factory.h"
#include "engine/transform/transform.h"

#include "editor/events.h"

#include "editor/gui/icons/component_icons.h"
#include "editor/gui/inspectables/entity_inspectable.h"
#include "editor/gui/inspector_panel.h"
#include "editor/gui/utils/gui_utils.h"
#include "editor/gui/viewport/action_overlay.h"
#include "editor/runtime/runtime.h"

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
      dragging_hierarchy(false),
      hierarchy_dirty_(true) {

  /* TODO: setup drag rect here */

  // Subscribe to transform creation/destruction
  auto& reg = ECS::Main().Reg();

  on_create_connection_ = reg.on_construct<TransformComponent>()
                              .connect<&HierarchyPanel::OnEntityChanged>(this);
  on_destroy_connection_ = reg.on_destroy<TransformComponent>()
                               .connect<&HierarchyPanel::OnEntityChanged>(this);
  on_camera_create_connection_ =
      reg.on_construct<CameraComponent>()
          .connect<&HierarchyPanel::OnEntityChanged>(this);
  on_camera_destroy_connection_ =
      reg.on_destroy<CameraComponent>()
          .connect<&HierarchyPanel::OnEntityChanged>(this);
}

//=============================================================================
// RENDER
//=============================================================================
void HierarchyPanel::Render() {
  if (hierarchy_dirty_) {
    BuildSceneHierarchy();
    hierarchy_dirty_ = false;
  }

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  GUIUtils::HideDockTabBar();
  ImGui::Begin("Hierarchy", nullptr, EditorFlag::fixed);
  ImGui::PopStyleVar();
  {
    RenderPopupMenu();
    ImDrawList& draw_list = *ImGui::GetWindowDrawList();
    RenderHeader(draw_list);
    RenderHierarchy(draw_list);
    HandleShortcuts();
    RenderDraggedItem();
    PerformAutoScroll();
  }
  ImGui::End();

  // Apply the queued entity changes now that the tree is no longer in use
  if (!pending_.empty()) {
    const auto actions = std::move(pending_);
    pending_.clear();
    for (const auto& action : actions)
      action();
    hierarchy_dirty_ = true;
  }
}

void HierarchyPanel::RenderSearch(ImDrawList& draw_list, ImVec2 position,
                                  ImVec2 size) {
  IMComponents::SearchField(draw_list, "##HierarchySearch", search_buffer,
                            IM_ARRAYSIZE(search_buffer), position, size,
                            "Search...", actions_width_);
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

  // [ + v ] [ (q) Search...            ]
  const float actions_x = strip_min.x + header_pad_x_;
  RenderActions(draw_list, ImVec2(actions_x, y), search_height_);

  const ImVec2 search_pos = ImVec2(actions_x + actions_width_, y);
  const ImVec2 search_size =
      ImVec2(std::max(0.0f, strip_max.x - header_pad_x_ - search_pos.x),
             search_height_);
  RenderSearch(draw_list, search_pos, search_size);

  // Divider between header strip and tree
  draw_list.AddLine(ImVec2(strip_min.x, strip_max.y - 0.5f),
                    ImVec2(strip_max.x, strip_max.y - 0.5f),
                    EditorColor::line_subtle, 1.0f);

  ImGui::SetCursorScreenPos(ImVec2(strip_min.x, strip_max.y + content_gap_));
}

void HierarchyPanel::RenderActions(ImDrawList& draw_list, ImVec2 position,
                                   float height) {
  if (IMComponents::DropdownButton(draw_list, "##HierarchyActions",
                                   ICON_MDI_PLUS, position, height, true,
                                   actions_width_,
                                   ImDrawFlags_RoundCornersLeft)) {
    PopupMenu::Open(ImVec2(position.x, position.y + height + 2.0f));
  }
}

void HierarchyPanel::RenderHierarchy(ImDrawList& draw_list) {
  // Follow selections made elsewhere (viewport picking, inspector)
  const Entity active = Runtime::State().selected_entity;
  if (active == entt::null) {
    selected_.clear();
  } else if (!selected_.count(active)) {
    selected_ = {active};
    last_selected_ = active;
  }

  hovered_ = entt::null;
  drawn_rows_.clear();

  ImGui::PushFont(EditorStyles::GetFonts().p);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

  RenderRootHeader(draw_list);

  if (root_expanded_) {
    const ImVec2 children_start = ImGui::GetCursorScreenPos();
    row_index_ = 0;

    float last_row_center = children_start.y;
    for (auto& item : current_hierarchy) {
      if (!MatchesSearch(item))
        continue;
      last_row_center = ImGui::GetCursorScreenPos().y + row_height_ * 0.5f;
      RenderItem(draw_list, item, 1);
    }

    // Vertical guide under the root caret, down to the last child's connector
    DrawGuide(draw_list, children_start.x + row_pad_x_, children_start.y,
              last_row_center);
  }

  ImGui::PopStyleVar();
  ImGui::PopFont();

  std::swap(visible_rows_, drawn_rows_);
}

void HierarchyPanel::RenderRootHeader(ImDrawList& draw_list) {
  const ImVec2 rect_min = ImGui::GetCursorScreenPos();
  const ImVec2 rect_max = ImVec2(rect_min.x + ImGui::GetContentRegionAvail().x,
                                 rect_min.y + row_height_);

  const bool hovered =
      ImGui::IsMouseHoveringRect(rect_min, rect_max) && !popup_menu_used;

  // The scene row stands apart from the entity rows below it
  draw_list.AddRectFilled(rect_min, rect_max, EditorColor::surface_3, 2.0f);
  if (hovered)
    draw_list.AddRectFilled(rect_min, rect_max,
                            dragging_hierarchy ? EditorColor::accent_deep
                                               : EditorColor::hover_overlay,
                            2.0f);
  if (hovered && ImGui::IsMouseClicked(0))
    root_expanded_ = !root_expanded_;

  // Dropping on the scene row moves the entities to the root
  if (hovered && dragging_hierarchy && !ImGui::IsMouseDown(0))
    MoveEntities(DraggedEntities(), entt::null);

  const float x0 = rect_min.x + row_pad_x_;
  DrawChevron(draw_list, ImVec2(x0, rect_min.y), root_expanded_, true,
              EditorColor::chevron);

  DrawGlyph(draw_list, ImVec2(x0 + caret_slot_, rect_min.y),
            ImVec2(icon_slot_, row_height_), ICON_MDI_MOVIE_OPEN_OUTLINE,
            EditorColor::accent_hover);

  // Open scene's name in bold, with * while it has unsaved changes
  const ProjectManager& project = Runtime::Project();
  const std::string name = project.SceneName();
  ImFont* font = EditorStyles::GetFonts().p_bold;
  const float font_size = ImGui::GetFontSize();
  ImVec2 text_pos =
      ImVec2(x0 + caret_slot_ + icon_slot_ + text_gap_,
             rect_min.y + (row_height_ - font_size) * 0.5f);
  draw_list.AddText(font, font_size, text_pos, EditorColor::text_bright,
                    name.c_str());
  if (project.IsSceneDirty()) {
    text_pos.x += font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, name.c_str()).x;
    draw_list.AddText(font, font_size, text_pos, EditorColor::text_dim, "*");
  }

  if (hovered && !dragging_hierarchy)
    ImGui::SetTooltip("%s", project.ScenePath().empty()
                                ? "Unsaved scene"
                                : project.ScenePath().generic_string().c_str());

  ImGui::Dummy(ImVec2(0.0f, row_height_));
}

void HierarchyPanel::RenderItem(ImDrawList& draw_list, HierarchyItem& item,
                                uint32_t indentation) {
  ImDrawList* foreground_draw_list = ImGui::GetForegroundDrawList();
  ImGuiIO& io = ImGui::GetIO();

  // EVALUATE
  const Entity entity = item.entity.Handle();
  const bool selected = selected_.count(entity) > 0;
  const bool expanded = expanded_.count(entity) > 0;
  const bool renaming = renaming_ == entity;
  const bool has_children = item.children.size() > 0;
  const float x_offset = indentation * indent_;

  const ImVec2 cursor_position = ImGui::GetCursorScreenPos();
  const ImVec2 content_region = ImGui::GetContentRegionAvail();
  const ImVec2 mouse_position = ImGui::GetMousePos();

  const ImVec2 rect_min = cursor_position;
  const ImVec2 rect_max = ImVec2(cursor_position.x + content_region.x,
                                 cursor_position.y + row_height_);
  const ImVec2 final_size = rect_max - rect_min;

  // ZEBRA STRIPE
  if (row_index_++ % 2 == 1)
    draw_list.AddRectFilled(rect_min, rect_max, EditorColor::row_alt);

  const bool hovered =
      ImGui::IsMouseHoveringRect(rect_min, rect_max) && !popup_menu_used;
  const bool double_clicked = hovered && ImGui::IsMouseDoubleClicked(0);
  const bool wheel_clicked = hovered && ImGui::IsMouseClicked(2);
  const bool dragging_this = hovered && ImGui::IsMouseDragging(0);

  if (hovered)
    hovered_ = entity;
  drawn_rows_.push_back(entity);

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
                                          : EditorColor::surface_4;
  if (drop_type == DROP_ITEM)
    row_color = EditorColor::accent_deep;
  if (row_color != 0)
    draw_list.AddRectFilled(rect_min, rect_max, row_color, 2.0f);

  // FOREGROUND COLORS
  const ImU32 text_color =
      selected ? EditorColor::accent_border : EditorColor::text;
  const ImU32 chevron_color =
      selected ? EditorColor::accent_border : EditorColor::chevron;

  // RIGHT-ALIGNED ICONS (data / eye / camera) — drawn before hit-testing the
  // row so clicking a toggle doesn't also select
  const bool icon_clicked =
      RenderItemIcons(draw_list, item, rect_min, rect_max);

  // CONNECTOR: short stub from the parent's guide, stopping at this row's
  // caret column (the icon keeps its gap)
  const ImVec2 caret_min =
      ImVec2(rect_min.x + row_pad_x_ + x_offset, rect_min.y);
  const float guide_x = std::floor(caret_min.x - indent_ + caret_slot_ * 0.5f);
  // 1px filled rect, not AddLine: an anti-aliased line can land between
  // pixels and smear over two, reading thicker
  const float row_center = std::floor(rect_min.y + row_height_ * 0.5f);
  draw_list.AddRectFilled(
      ImVec2(guide_x, row_center),
      ImVec2(std::floor(caret_min.x) - 3.0f, row_center + 1.0f),
      EditorColor::tree_guide);

  // CHEVRON (only when there are children)
  bool chevron_clicked = false;
  if (has_children) {
    chevron_clicked =
        DrawChevron(draw_list, caret_min, expanded, true, chevron_color);
    if (chevron_clicked || wheel_clicked) {
      if (expanded)
        expanded_.erase(entity);
      else
        expanded_.insert(entity);
    }
  }

  // TYPE ICON + NAME
  DrawGlyph(draw_list, ImVec2(caret_min.x + caret_slot_, rect_min.y),
            ImVec2(icon_slot_, row_height_), EntityIcon(item),
            EditorColor::accent_border);

  const ImVec2 text_pos =
      ImVec2(caret_min.x + caret_slot_ + icon_slot_ + text_gap_,
             rect_min.y + (row_height_ - ImGui::GetFontSize()) * 0.5f);
  if (renaming)
    RenderRename(ImVec2(text_pos.x, rect_min.y),
                 rect_max.x - row_pad_x_ - toggle_slot_ * 3 - text_pos.x);
  else
    draw_list.AddText(text_pos, text_color, item.entity.Name().c_str());

  // SELECTION (unchanged logic; ignores clicks consumed by chevron/icons)
  const bool clicked =
      hovered && ImGui::IsMouseClicked(0) && !chevron_clicked && !icon_clicked;
  if (clicked) {
    if (io.KeyCtrl) {
      if (!selected) {
        Select(entity, true);
      } else {
        selected_.erase(entity);
        // Hand the inspector another selected entity, or none
        if (Runtime::State().selected_entity == entity) {
          if (selected_.empty())
            Runtime::State().ClearSelection();
          else
            Runtime::State().SelectEntity(*selected_.begin());
        }
      }
    } else if (io.KeyShift && last_selected_ != entt::null) {
      // Range over last frame's rows, in the order they were drawn
      auto from = std::find(visible_rows_.begin(), visible_rows_.end(),
                            last_selected_);
      auto to = std::find(visible_rows_.begin(), visible_rows_.end(), entity);
      if (from != visible_rows_.end() && to != visible_rows_.end()) {
        if (from > to)
          std::swap(from, to);
        selected_.insert(from, to + 1);
      }
      Runtime::State().SelectEntity(entity);
    } else if (!selected || selected_.size() == 1) {
      // Clicking inside a multi-selection keeps it, so it can be dragged
      Select(entity, false);
    }
  }

  // FOCUS CAMERA
  if (double_clicked && !renaming) {
    EditorEvents::focus_entity(item.entity.Handle());
    Viewport::PostAction("Focus: " + item.entity.Name());
  }

  // ADVANCE CURSOR
  ImGui::Dummy(ImVec2(0.0f, row_height_));

  // START DRAG (unchanged)
  if (dragging_this && !dragging_hierarchy && selected && !renaming)
    dragging_hierarchy = true;

  // DROP: onto the row makes a child, above/below places next to it
  if (dragging_hierarchy && !ImGui::IsMouseDown(0) && drop_type != NO_DROP) {
    if (drop_type == DROP_ITEM) {
      MoveEntities(DraggedEntities(), entity);
      expanded_.insert(entity);
    } else {
      MoveEntities(DraggedEntities(), item.entity.Transform().parent_, entity,
                   drop_type == MOVE_ITEM_DOWN);
    }
  }

  // CHILDREN (auto-expanded while searching)
  if (has_children && (expanded || Searching())) {
    const float children_top = ImGui::GetCursorScreenPos().y;
    float last_row_center = children_top;
    for (auto& child : item.children) {
      if (!MatchesSearch(child))
        continue;
      last_row_center = ImGui::GetCursorScreenPos().y + row_height_ * 0.5f;
      RenderItem(draw_list, child, indentation + 1);
    }
    DrawGuide(draw_list, caret_min.x, children_top, last_row_center);
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
  // ImGui's own tree-node arrow, at the scale TreeNode uses. RenderArrow
  // centers it at pos + (0.5h, 0.35h) for font size h
  const float h = ImGui::GetFontSize();
  const ImVec2 pos =
      ImVec2(std::floor(slot_min.x + caret_slot_ * 0.5f - h * 0.5f),
             std::floor(slot_min.y + row_height_ * 0.5f - h * 0.35f));
  ImGui::RenderArrow(&draw_list, pos,
                     hovered ? EditorColor::text_bright : color,
                     expanded ? ImGuiDir_Down : ImGuiDir_Right, 0.70f);
  return hovered && ImGui::IsMouseClicked(0);
}

// Vertical tree guide under the caret slot starting at caret_x
void HierarchyPanel::DrawGuide(ImDrawList& draw_list, float caret_x,
                               float top, float bottom) {
  // 1px filled rect for a crisp line (see the connector in RenderItem)
  const float x = std::floor(caret_x + caret_slot_ * 0.5f);
  draw_list.AddRectFilled(ImVec2(x, std::floor(top)),
                          ImVec2(x + 1.0f, std::floor(bottom) + 1.0f),
                          EditorColor::tree_guide);
}

const char* HierarchyPanel::EntityIcon(const HierarchyItem& item) const {
  return EditorIcons::GetEntityIcon(item.entity.Handle());
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

  size_t n_selected = selected_.size();
  auto& world = ECS::Main();
  std::string text =
      n_selected > 1 ? std::to_string(n_selected) + " selected"
      : world.Has<TransformComponent>(last_selected_)
          ? "Moving " + world.Get<TransformComponent>(last_selected_).name_
          : "Moving";

  std::string label = std::string(ICON_FA_LEFT_LONG) + "   " + text;

  ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
  ImVec2 padding = ImVec2(20.0f, 10.0f);
  ImVec2 rect_min = pos;
  ImVec2 rect_max = pos + text_size + padding * 2;

  fg->AddRectFilled(rect_min, rect_max, EditorColor::accent_deep, 5.0f);
  fg->AddText(pos + padding, IM_COL32(255, 255, 255, 255), label.c_str());
}

void HierarchyPanel::RenderPopupMenu() {
  if (PopupMenu::Begin()) {
    popup_menu_used = true;

    // The menu acts on the row it opened over (none from + or empty space);
    // right-clicking an unselected row selects it first
    if (ImGui::IsWindowAppearing()) {
      popup_target_ = hovered_;
      if (hovered_ != entt::null && !selected_.count(hovered_))
        Select(hovered_, false);
    }

    if (popup_target_ != entt::null) {
      if (PopupMenu::Item(ICON_FA_TRASH, "Delete"))
        DeleteSelected();
      if (PopupMenu::Item(ICON_FA_COPY, "Duplicate"))
        DuplicateSelected();
      if (PopupMenu::Item(ICON_FA_PENCIL, "Rename"))
        StartRename(popup_target_);

      PopupMenu::Separator();
    }

    // New entities go under the row the menu opened over
    if (PopupMenu::Item(ICON_FA_PLUS, "Empty Entity"))
      CreateEntity("Entity");

    if (PopupMenu::Item(ICON_FA_CAMERA, "Camera"))
      CreateEntity("Camera",
                   [](Entity e) { ECS::Main().Add<CameraComponent>(e); });

    if (PopupMenu::Menu(ICON_FA_CUBE, "3D Primitives")) {
      if (PopupMenu::ItemLight("Cube"))
        CreateEntity("Cube", [](Entity e) {
          ModelEntityFactory::AttachPrimitive(ECS::Main(), e, "cube");
        });
      if (PopupMenu::ItemLight("Sphere"))
        CreateEntity("Sphere", [](Entity e) {
          ModelEntityFactory::AttachPrimitive(ECS::Main(), e, "sphere");
        });
      if (PopupMenu::ItemLight("Capsule"))
        CreateEntity("Capsule", [](Entity e) {
          ModelEntityFactory::AttachPrimitive(ECS::Main(), e, "capsule");
        });
      if (PopupMenu::ItemLight("Cylinder"))
        CreateEntity("Cylinder", [](Entity e) {
          ModelEntityFactory::AttachPrimitive(ECS::Main(), e, "cylinder");
        });
      if (PopupMenu::ItemLight("Pyramid"))
        CreateEntity("Pyramid", [](Entity e) {
          ModelEntityFactory::AttachPrimitive(ECS::Main(), e, "pyramid");
        });
      if (PopupMenu::ItemLight("Plane"))
        CreateEntity("Plane", [](Entity e) {
          ModelEntityFactory::AttachPrimitive(ECS::Main(), e, "plane");
        });

      PopupMenu::EndMenu();
    }

    if (PopupMenu::Menu(ICON_FA_LIGHTBULB, "Light")) {
      if (PopupMenu::ItemLight("Directional Light"))
        CreateEntity("Directional Light", [](Entity e) {
          ECS::Main().Add<DirectionalLightComponent>(e);
        });
      if (PopupMenu::ItemLight("Point Light"))
        CreateEntity("Point Light", [](Entity e) {
          ECS::Main().Add<PointLightComponent>(e);
        });
      if (PopupMenu::ItemLight("Spotlight"))
        CreateEntity("Spotlight",
                     [](Entity e) { ECS::Main().Add<SpotlightComponent>(e); });

      PopupMenu::EndMenu();
    }

    if (PopupMenu::Menu(ICON_FA_VOLUME_HIGH, "Audio")) {
      if (PopupMenu::ItemLight("Audio Source")) { /* TODO: Handle audio source */
      }

      PopupMenu::EndMenu();
    }

    if (PopupMenu::Menu(ICON_FA_DIAGRAM_PROJECT, "PCG")) {
      // ? MeshRendererComponent should be added later when generator produces
      // geometry
      if (PopupMenu::ItemLight("PCG Graph"))
        CreateEntity("PCG Graph",
                     [](Entity e) { ECS::Main().Add<VolumeComponent>(e); });

      PopupMenu::EndMenu();
    }

    PopupMenu::End();
  } else {
    popup_menu_used = false;
    popup_target_ = entt::null;
  }

  PopupMenu::Pop();
}

void HierarchyPanel::RenderRename(const ImVec2& position, float width) {
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  const float frame_height = ImGui::GetFontSize() + 2.0f;
  ImGui::SetCursorScreenPos(ImVec2(
      position.x - 2.0f, position.y + (row_height_ - frame_height) * 0.5f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 1.0f));
  ImGui::SetNextItemWidth(std::max(width, 40.0f));

  if (rename_focus_) {
    ImGui::SetKeyboardFocusHere();
    rename_focus_ = false;
  }
  const bool entered = ImGui::InputText(
      "##Rename", rename_buffer_, sizeof(rename_buffer_),
      ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
  const bool done = entered || ImGui::IsItemDeactivated();

  ImGui::PopStyleVar();
  ImGui::SetCursorScreenPos(cursor);

  if (!done)
    return;

  // Enter or clicking away keeps the name, Esc cancels
  if (!ImGui::IsKeyPressed(ImGuiKey_Escape) && rename_buffer_[0] != '\0') {
    const Entity entity = renaming_;
    const std::string name = rename_buffer_;
    Defer([entity, name] {
      if (ECS::Main().Has<TransformComponent>(entity))
        ECS::Main().Get<TransformComponent>(entity).name_ = name;
      Runtime::Project().MarkSceneDirty();
    });
  }
  renaming_ = entt::null;
}

//=============================================================================
// ENTITY ACTIONS
//=============================================================================
void HierarchyPanel::HandleShortcuts() {
  if (!GUIUtils::WindowFocused() || ImGui::GetIO().WantTextInput)
    return;

  if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
    DeleteSelected();
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D))
    DuplicateSelected();
  if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && last_selected_ != entt::null)
    StartRename(last_selected_);

  // FOCUS: F
  if (ImGui::IsKeyChordPressed(ImGuiKey_F)) {
    const Entity selected = Runtime::State().selected_entity;
    if (selected != entt::null &&
        ECS::Main().Has<TransformComponent>(selected)) {
      EditorEvents::focus_entity(selected);
      Viewport::PostAction(
          "Focus: " + ECS::Main().Get<TransformComponent>(selected).name_, "F");
    }
  }
}

void HierarchyPanel::CreateEntity(const std::string& name,
                                  std::function<void(Entity)> setup) {
  const Entity parent = popup_target_;
  Defer([this, name, setup, parent] {
    auto& world = ECS::Main();
    const bool has_parent = world.Has<TransformComponent>(parent);
    const Entity entity = has_parent
                              ? std::get<0>(world.CreateEntity(name, parent))
                              : std::get<0>(world.CreateEntity(name));
    if (setup)
      setup(entity);
    if (has_parent)
      expanded_.insert(parent);
    Select(entity, false);
    StartRename(entity);
    Runtime::Project().MarkSceneDirty();
  });
}

void HierarchyPanel::DeleteSelected() {
  const std::vector<Entity> targets = DraggedEntities();
  Defer([this, targets] {
    auto& world = ECS::Main();
    for (Entity entity : targets) {
      if (!world.Has<TransformComponent>(entity))
        continue;
      if (world.IsAncestorOf(entity, Runtime::State().selected_entity))
        Runtime::State().ClearSelection();
      world.DestroyEntity(entity);
    }
    selected_.clear();
    last_selected_ = entt::null;
    Runtime::Project().MarkSceneDirty();
  });
}

void HierarchyPanel::DuplicateSelected() {
  const std::vector<Entity> targets = DraggedEntities();
  Defer([this, targets] {
    auto& world = ECS::Main();
    selected_.clear();
    for (Entity entity : targets) {
      const Entity copy = world.DuplicateEntity(entity);
      if (copy == entt::null)
        continue;

      // Place the copy right below the original
      if (Transform::HasParent(world.Get<TransformComponent>(entity))) {
        world.MoveNextTo(copy, entity, true);
      } else {
        auto it = std::find(root_order_.begin(), root_order_.end(), entity);
        root_order_.insert(it == root_order_.end() ? it : it + 1, copy);
      }
      Select(copy, true);
    }
    Runtime::Project().MarkSceneDirty();
  });
}

void HierarchyPanel::StartRename(Entity entity) {
  if (!ECS::Main().Has<TransformComponent>(entity))
    return;
  renaming_ = entity;
  rename_focus_ = true;
  const std::string& name = ECS::Main().Get<TransformComponent>(entity).name_;
  std::snprintf(rename_buffer_, sizeof(rename_buffer_), "%s", name.c_str());
}

void HierarchyPanel::Select(Entity entity, bool additive) {
  if (!additive)
    selected_.clear();
  selected_.insert(entity);
  last_selected_ = entity;
  Runtime::State().SelectEntity(entity);
}

std::vector<Entity> HierarchyPanel::DraggedEntities() const {
  auto& world = ECS::Main();
  auto parent_selected = [&](Entity entity) {
    Entity parent = world.Get<TransformComponent>(entity).parent_;
    for (; world.Has<TransformComponent>(parent);
         parent = world.Get<TransformComponent>(parent).parent_) {
      if (selected_.count(parent))
        return true;
    }
    return false;
  };

  // Tree order; a selected row hidden by a collapsed parent moves with it
  std::vector<Entity> entities;
  for (Entity entity : visible_rows_) {
    if (selected_.count(entity) && world.Has<TransformComponent>(entity) &&
        !parent_selected(entity))
      entities.push_back(entity);
  }
  return entities;
}

void HierarchyPanel::MoveEntities(const std::vector<Entity>& entities,
                                  Entity parent, Entity sibling, bool after) {
  Defer([this, entities, parent, sibling, after]() mutable {
    auto& world = ECS::Main();
    Runtime::Project().MarkSceneDirty();
    for (Entity entity : entities) {
      // No dropping an entity into itself or its own subtree
      if (entity == sibling || world.IsAncestorOf(entity, parent))
        continue;
      // Keep the entity where it is in the world under its new parent
      TransformComponent& transform = world.Get<TransformComponent>(entity);
      if (transform.parent_ != parent) {
        const glm::mat4 model = Transform::GetModel(transform, Space::WORLD);
        if (parent == entt::null)
          world.RemoveParent(entity);
        else
          world.SetParent(entity, parent);
        Transform::SetModel(transform, model, Space::WORLD);
      }
      if (sibling == entt::null)
        continue;

      if (parent != entt::null) {
        world.MoveNextTo(entity, sibling, after);
      } else {
        root_order_.erase(
            std::remove(root_order_.begin(), root_order_.end(), entity),
            root_order_.end());
        auto it = std::find(root_order_.begin(), root_order_.end(), sibling);
        if (after && it != root_order_.end())
          ++it;
        root_order_.insert(it, entity);
      }
      // Each next entity goes below the previous one, keeping their order
      if (after)
        sibling = entity;
    }
  });
}

void HierarchyPanel::BuildSceneHierarchy() {
  current_hierarchy.clear();

  auto& world = ECS::Main();
  auto is_root = [&](Entity entity) {
    return world.Has<TransformComponent>(entity) &&
           !Transform::HasParent(world.Get<TransformComponent>(entity));
  };

  // Keep the known order; drop entities that were destroyed or got a parent
  root_order_.erase(std::remove_if(root_order_.begin(), root_order_.end(),
                                   [&](Entity e) { return !is_root(e); }),
                    root_order_.end());

  // New roots go last, oldest first (views iterate newest first)
  const std::unordered_set<Entity> known(root_order_.begin(),
                                         root_order_.end());
  std::vector<Entity> found;
  for (Entity entity : world.View<TransformComponent>()) {
    if (is_root(entity) && !known.count(entity))
      found.push_back(entity);
  }
  root_order_.insert(root_order_.end(), found.rbegin(), found.rend());

  for (Entity entity : root_order_) {
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