#ifndef INSPECTABLE_BASE_H
#define INSPECTABLE_BASE_H

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

#include <vector>

#include "editor/pipelines/preview_pipeline.h"

class AssetPreview;

class InspectableBase {

public:
  virtual ~InspectableBase() = default;
  virtual void RenderStaticContent(ImDrawList& draw_list) = 0;
  virtual void RenderDynamicContent(ImDrawList& draw_list) = 0;
  // What the preview under the inspector shows; none: no preview
  virtual std::vector<PreviewMesh> PreviewMeshes() { return {}; }
  // What the item adds to its preview: buttons in its header (with
  // AssetPreview::Button), drawing over its view and rows under it
  virtual void RenderPreviewButtons(AssetPreview& preview) {}
  virtual void RenderPreviewOverlay(AssetPreview& preview) {}
  virtual void RenderPreviewRows(AssetPreview& preview) {}
};

#endif  // INSPECTABLE_BASE_H