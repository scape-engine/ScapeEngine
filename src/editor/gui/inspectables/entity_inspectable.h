#ifndef ENTITY_INSPECTABLE_H
#define ENTITY_INSPECTABLE_H

#include "inspectable_base.h"

#include <memory>

#include "editor/gui/hierarchy_panel.h"
#include "editor/gui/procmodel_preview.h"
#include "engine/ecs/ecs_collection.h"

class EntityInspectable : public InspectableBase {
public:
  EntityInspectable(HierarchyItem& item);

  void RenderStaticContent(ImDrawList& draw_list) override;
  void RenderDynamicContent(ImDrawList& draw_list) override;
  // The selected instance's object, else its meshes and its children's
  std::vector<PreviewMesh> PreviewMeshes() override;
  // A procedural model's instance adds its seeds, parts and origin
  void RenderPreviewButtons(AssetPreview& preview) override;
  void RenderPreviewOverlay(AssetPreview& preview) override;
  void RenderPreviewRows(AssetPreview& preview) override;

private:
  // The selected instance of its InstancedMeshComponent; -1: none
  int SelectedInstance() const;
  // The selected instance's procedural model; null: not one
  ProcModelPreview* ProcModel();

  HierarchyItem item;
  std::unique_ptr<ProcModelPreview> procmodel_;
  int procmodel_instance_ = -1;  // the instance procmodel_ is of
};

#endif  // ENTITY_INSPECTABLE_H