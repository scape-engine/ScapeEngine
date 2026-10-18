#ifndef ACTION_OVERLAY_H
#define ACTION_OVERLAY_H

#include <imgui.h>
#include <string>

namespace Viewport {

// Show a short message in the viewport's bottom-left corner, replacing the
// current one. key: optional shortcut drawn as a keycap, e.g. "F"
void PostAction(const std::string& text, const char* key = nullptr);

// Draw the current message; it fades out after a short delay
void ActionOverlay(ImVec2 canvas_min, ImVec2 canvas_size);

}  // namespace Viewport

#endif  // ACTION_OVERLAY_H