#ifndef EDITOR_STATE_H
#define EDITOR_STATE_H

#include <entt/entt.hpp>
#include "editor/systems/asset_meta.h"
#include "events.h"

using Entity = entt::entity;

struct EditorState {
  // Selection
  Entity selected_entity = entt::null;
  Entity last_selected_entity = entt::null;
  // A project asset; 0: none. One of the entity and the asset is selected
  AssetSID selected_asset = 0;

  void SelectEntity(Entity entity) {
    if (entity == entt::null)
      return;
    selected_asset = 0;
    selected_entity = entity;
    last_selected_entity = entity;
    EditorEvents::entity_selected(entity);
  }

  void SelectAsset(AssetSID asset) {
    if (asset == 0)
      return;
    selected_entity = entt::null;
    selected_asset = asset;
    EditorEvents::asset_selected(asset);
  }

  void ClearSelection() {
    selected_entity = entt::null;
    selected_asset = 0;
    EditorEvents::selection_cleared();
  }
};

#endif  // EDITOR_STATE_H