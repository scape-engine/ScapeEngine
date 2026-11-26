#ifndef ASSET_PREVIEW_H
#define ASSET_PREVIEW_H

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

#include <glm/glm.hpp>

#include <vector>

#include "editor/pipelines/preview_pipeline.h"

class InspectableBase;

// A turntable view of an item's meshes, like the preview under Unity's
// inspector: dragging turns it, the wheel zooms. Its own buttons light it,
// show its bounds, swap the background, step through its LODs and reset the
// camera; the item adds its own buttons, overlays and rows
// (InspectableBase's Preview hooks), so this knows nothing of what made it
class AssetPreview {
public:
  AssetPreview();
  AssetPreview(const AssetPreview&) = delete;
  AssetPreview& operator=(const AssetPreview&) = delete;

  // Back to the first view, for a new item
  void Reset();

  // The buttons, right to left from `top_right` on a row `height` tall:
  // its own, then the item's
  void RenderButtons(InspectableBase& item, ImVec2 top_right, float height);
  // One button on that row; true when clicked. `color`: its glyph while off
  bool Button(const char* icon, bool on, const char* tooltip,
              ImU32 color = 0);

  // The view of the item's meshes at the cursor, `size` big with its facts
  // and the item's rows (which take what they need from the bottom)
  void RenderView(InspectableBase& item, ImVec2 size);

  // Where `point`, in the item's preview space, shows on screen (the last
  // view drawn); for overlays
  ImVec2 Project(const glm::vec3& point) const;
  // The view's rectangle on screen
  ImVec2 ViewMin() const { return view_min_; }
  ImVec2 ViewMax() const { return view_max_; }

private:
  size_t output_;  // PreviewPipeline's; it keeps its outputs

  // Turned about the meshes, and the zoom
  float yaw_ = 0.6f;
  float pitch_ = 0.35f;
  float zoom_ = 1.0f;

  bool lit_ = true;
  bool bounds_ = false;
  bool light_background_ = false;
  int lod_ = 0;  // 0: the meshes themselves, n: their nth LOD

  // The button row being laid out: its right end and height
  ImVec2 button_at_{0.0f, 0.0f};
  float button_size_ = 0.0f;

  // The last view: item space to clip space, and where it showed
  glm::mat4 view_projection_{1.0f};
  ImVec2 view_min_{0.0f, 0.0f};
  ImVec2 view_max_{0.0f, 0.0f};
  // The item's rows' height last frame, kept off the view's
  float rows_height_ = 0.0f;
};

#endif  // ASSET_PREVIEW_H
