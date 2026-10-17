#ifndef VIEWPORT_OVERLAY_H
#define VIEWPORT_OVERLAY_H

#include <imgui.h>
#include <cmath>

//---------------------------------------------------------------------------
// VIEWPORT OVERLAY HELPERS
// Shared placement for overlays drawn over the viewport canvas.
// Utilities only: overlays don't implement anything from this file.
//---------------------------------------------------------------------------
namespace Viewport {

enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };

// Screen position of the top-left corner for content of content_size placed
// in the given corner of the canvas, inset by margin. Snapped to whole pixels.
// Use with ImGui::SetCursorScreenPos (screen space, not window-local)
inline ImVec2 AnchorToCorner(Corner corner, ImVec2 canvas_min,
                             ImVec2 canvas_size, ImVec2 content_size,
                             ImVec2 margin) {
  const bool right =
      corner == Corner::TopRight || corner == Corner::BottomRight;
  const bool bottom =
      corner == Corner::BottomLeft || corner == Corner::BottomRight;

  const float x = right
                      ? canvas_min.x + canvas_size.x - margin.x - content_size.x
                      : canvas_min.x + margin.x;
  const float y = bottom
                      ? canvas_min.y + canvas_size.y - margin.y - content_size.y
                      : canvas_min.y + margin.y;
  return ImVec2(std::floor(x), std::floor(y));
}

}  // namespace Viewport

#endif  // VIEWPORT_OVERLAY_H