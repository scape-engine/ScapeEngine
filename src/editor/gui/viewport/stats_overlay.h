#ifndef STATS_OVERLAY_H
#define STATS_OVERLAY_H

#include <imgui.h>

namespace Viewport {

void StatsOverlay(ImVec2 canvas_min, ImVec2 canvas_size, float camera_speed,
                  bool& visible);

}  // namespace Viewport

#endif  // STATS_OVERLAY_H