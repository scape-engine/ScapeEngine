#ifndef STATS_OVERLAY_H
#define STATS_OVERLAY_H

#include <imgui.h>

namespace Viewport {

// Debug stats panel in the canvas's bottom-right corner; F3 toggles it
void StatsOverlay(ImVec2 canvas_min, ImVec2 canvas_size, float camera_speed);

}  // namespace Viewport

#endif  // STATS_OVERLAY_H