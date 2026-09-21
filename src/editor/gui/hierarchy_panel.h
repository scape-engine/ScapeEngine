#ifndef HIERARCHY_PANEL_H
#define HIERARCHY_PANEL_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "window_base.h"

#include "editor/gui/styles/editor_styles.h"
#include "engine/ecs/ecs_collection.h"

//---------------------------------------------------------------------------
// TODO:
// - search/filter function
// - rename context menu implementation
// - camera movement integration
// - rename EditorColor to actual theme usage
// - replace direct imgui code for drawing with dynamic drawing impl
// - fg
//---------------------------------------------------------------------------
struct HierarchyItem {
  explicit HierarchyItem(EntityContainer entity,
                         std::vector<HierarchyItem> children = {})
      : entity(entity), children(children), expanded(false) {};

  EntityContainer entity;
  std::vector<HierarchyItem> children;
  bool expanded;

  // Placeholders for the row toggles (wired later)
  bool visible = true;     // eye
  bool renderable = true;  // camera

  bool operator==(const HierarchyItem& other) const {
    return entity.Id() == other.entity.Id();
  }
};

//---------------------------------------------------------------------------
// HIERARCHY PANEL
// - Includes scene graph view with tree, drag-drop reparenting, multi-selection
//---------------------------------------------------------------------------
class HierarchyPanel : public WindowBase {
public:
  HierarchyPanel();

  void Render() override;
  void Invalidate();

private:
  // Rendering
  void RenderHeader(ImDrawList& draw_list); 
 void RenderSearch(ImDrawList& draw_list, ImVec2 position, ImVec2 size);
 void RenderActions(ImDrawList& draw_list, ImVec2 position, float height);
  void RenderHierarchy(ImDrawList& draw_list);
  void RenderRootHeader(ImDrawList& draw_list);
  void RenderItem(ImDrawList& draw_list, HierarchyItem& item,
                  uint32_t indentation);
  bool RenderItemIcons(ImDrawList& draw_list, HierarchyItem& item,
                       const ImVec2& rect_min, const ImVec2& rect_max);
  void RenderDraggedItem();
  void RenderPopupMenu();

  // Row helpers
  // Draw glyph centered in a slot of width slot_width starting at slot_min
  void DrawGlyph(ImDrawList& draw_list, ImVec2 slot_min, ImVec2 slot_size,
               const char* glyph, ImU32 color, ImFont* font = nullptr);

  // Draw chevron in caret slot; returns true if the slot was clicked
  bool DrawChevron(ImDrawList& draw_list, ImVec2 slot_min, bool expanded, bool interactive,
                   ImU32 color);

  // Entity presentation
  const char* EntityIcon(const HierarchyItem& item) const;
  const char* EntityDataIcon(const HierarchyItem& item) const;

  // Search filtering
  bool MatchesSearch(const HierarchyItem& item) const;
  bool Searching() const { return search_buffer[0] != '\0'; }

  // Hierarchy building
  void BuildSceneHierarchy();
  void BuildHierarchyChildren(HierarchyItem& parent);

  // Camera movement to entity
  void SetCameraTarget(TransformComponent* target);
  void UpdateCameraMovement();

  // Auto-scroll when dragging
  void PerformAutoScroll();

  void OnEntityChanged(entt::registry&, entt::entity);

  // Layout (px)
  static constexpr float header_height_ = 42.0f;
  static constexpr float header_pad_x_ = 10.0f;
  static constexpr float search_height_ = 26.0f;
  static constexpr float search_width_ratio_ = 0.58f; 
  static constexpr float content_gap_ = 6.0f;     
  static constexpr float row_height_ = 24.0f;
  static constexpr float row_pad_x_ = 6.0f; 
  static constexpr float caret_slot_ = 24.0f;
  static constexpr float icon_slot_ = 24.0f;
  static constexpr float indent_ = 24.0f;
  static constexpr float text_gap_ = 4.0f;
  static constexpr float header_gap_ = 6.0f;

  // Data
  char search_buffer[256];
  bool popup_menu_used;
  bool root_expanded_ = true;

  std::vector<HierarchyItem> current_hierarchy;
  std::unordered_map<uint32_t, HierarchyItem*> selected_items;
  HierarchyItem* last_selected;
  HierarchyItem* last_hovered;

  // Drag state
  bool dragging_hierarchy;

  // Camera movement state
  bool camera_moving;
  float camera_movement_time;
  TransformComponent* camera_target;

  // Handle cache
  bool hierarchy_dirty_ = true;

  // EnTT handles
  entt::scoped_connection on_create_connection_;
  entt::scoped_connection on_destroy_connection_;
};

#endif  // HIERARCHY_PANEL_H