#ifndef HIERARCHY_PANEL_H
#define HIERARCHY_PANEL_H

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
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
      : entity(entity), children(children) {};

  EntityContainer entity;
  std::vector<HierarchyItem> children;

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
  void RenderRename(const ImVec2& text_pos, float width);

  // Entity actions. They only queue work: the tree holds pointers into the
  // registry, so the world changes after the frame's tree is drawn
  void Defer(std::function<void()> action) {
    pending_.push_back(std::move(action));
  }
  void HandleShortcuts();
  void CreateEntity(const std::string& name,
                    std::function<void(Entity)> setup = {});
  void DeleteSelected();
  void DuplicateSelected();
  void StartRename(Entity entity);
  void Select(Entity entity, bool additive);
  // Dragged rows: the selection minus entities whose parent is also selected
  std::vector<Entity> DraggedEntities() const;
  // Moves the entities under parent (null = scene root), keeping their world
  // transform. With a sibling, they're placed before/after it
  void MoveEntities(const std::vector<Entity>& entities, Entity parent,
                    Entity sibling = entt::null, bool after = false);

  // Row helpers
  // Draw glyph centered in a slot of width slot_width starting at slot_min
  void DrawGlyph(ImDrawList& draw_list, ImVec2 slot_min, ImVec2 slot_size,
                 const char* glyph, ImU32 color, ImFont* font = nullptr);

  // Draw chevron in caret slot; returns true if the slot was clicked
  void DrawGuide(ImDrawList& draw_list, float caret_x, float top, float bottom);
  bool DrawChevron(ImDrawList& draw_list, ImVec2 slot_min, bool expanded,
                   bool interactive, ImU32 color);

  // Entity presentation
  const char* EntityIcon(const HierarchyItem& item) const;
  const char* EntityDataIcon(const HierarchyItem& item) const;

  // Search filtering
  bool MatchesSearch(const HierarchyItem& item) const;
  bool Searching() const { return search_buffer[0] != '\0'; }

  // Hierarchy building
  void BuildSceneHierarchy();
  void BuildHierarchyChildren(HierarchyItem& parent);

  // Auto-scroll when dragging
  void PerformAutoScroll();

  void OnEntityChanged(entt::registry&, entt::entity);

  // Layout (px)
  static constexpr float header_height_ = 34.0f;
  static constexpr float header_pad_x_ = 10.0f;
  static constexpr float search_height_ = 22.0f;
  static constexpr float actions_width_ = 44.0f;  // [ + v ] button
  static constexpr float content_gap_ = 4.0f;
  static constexpr float row_height_ = 20.0f;
  static constexpr float row_pad_x_ = 6.0f;
  static constexpr float caret_slot_ = 16.0f;
  static constexpr float icon_slot_ = 18.0f;
  static constexpr float toggle_slot_ = 20.0f;
  static constexpr float indent_ = 16.0f;
  static constexpr float text_gap_ = 4.0f;
  static constexpr float icon_size_ = 14.0f;

  // Data
  char search_buffer[256];
  bool popup_menu_used;
  bool root_expanded_ = true;
  uint32_t row_index_ = 0;  // row counter for zebra striping

  std::vector<HierarchyItem> current_hierarchy;
  // ponytail: root order lives in the panel only (the owning camera group
  // reorders the transform storage); not saved with the scene yet
  std::vector<Entity> root_order_;
  std::unordered_set<Entity> selected_;
  std::unordered_set<Entity> expanded_;
  // Rows in drawing order: last frame's (shift-select, drag order) and this
  // frame's, swapped after drawing
  std::vector<Entity> visible_rows_;
  std::vector<Entity> drawn_rows_;
  Entity last_selected_ = entt::null;
  Entity hovered_ = entt::null;
  Entity popup_target_ = entt::null;  // row the context menu was opened on

  std::vector<std::function<void()>> pending_;

  // Inline rename
  Entity renaming_ = entt::null;
  bool rename_focus_ = false;
  char rename_buffer_[256] = {};

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
  // The owning camera group swaps transforms in storage when a camera is
  // added or removed, which moves the tree's cached transform pointers
  entt::scoped_connection on_camera_create_connection_;
  entt::scoped_connection on_camera_destroy_connection_;
};

#endif  // HIERARCHY_PANEL_H