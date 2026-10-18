#include "stats_overlay.h"

#include <cstdio>

#include "editor/gui/components/im_components.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/gui/viewport/viewport_overlay.h"
#include "engine/diagnostics/frame_stats.h"

namespace Viewport {

namespace {

bool g_visible = false;  // TODO: persist visibility between launches
ImVec2 g_size = ImVec2(0.0f, 0.0f);  // measured last frame, padding included

const ImVec2 kMargin =
    ImVec2(EditorSizes::overlay_margin, EditorSizes::overlay_margin);
const ImVec2 kPadding = ImVec2(10.0f, 8.0f);

// 1234 -> "1.2K", 2500000 -> "2.5M"
void FormatCount(char* buffer, size_t size, uint32_t value) {
  if (value >= 1000000)
    std::snprintf(buffer, size, "%.1fM", value / 1000000.0);
  else if (value >= 1000)
    std::snprintf(buffer, size, "%.1fK", value / 1000.0);
  else
    std::snprintf(buffer, size, "%u", value);
}

}  // namespace

void StatsOverlay(ImVec2 canvas_min, ImVec2 canvas_size, float camera_speed) {
  if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiKey_F3))
    g_visible = !g_visible;
  if (!g_visible)
    return;

  const Diagnostics::FrameStatsSnapshot& stats = Diagnostics::GetFrameStats();

  const ImVec2 pos = AnchorToCorner(Corner::BottomRight, canvas_min,
                                    canvas_size, g_size, kMargin);
  ImGui::SetCursorScreenPos(ImVec2(pos.x + kPadding.x, pos.y + kPadding.y));

  // Monospace so digits don't shift as values change
  ImGui::PushFont(EditorStyles::GetFonts().console);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 2.0f));
  IMComponents::BeginOverlay();

  char draws[16];
  char triangles[16];
  FormatCount(draws, sizeof(draws), stats.draw_calls);
  FormatCount(triangles, sizeof(triangles), stats.triangles);

  ImGui::Text("FPS       %8.1f", stats.fps);
  ImGui::Text("Frame     %8.2f ms", stats.frame_ms_avg);
  ImGui::Text("  min/max %5.2f / %.2f", stats.frame_ms_min, stats.frame_ms_max);
  ImGui::Text("CPU       %8.2f ms", stats.cpu_ms);
  if (stats.gpu_ms > 0.0f)
    ImGui::Text("GPU       %8.2f ms", stats.gpu_ms);
  else
    ImGui::Text("GPU            n/a");
  ImGui::Text("Draws     %8s", draws);
  ImGui::Text("Triangles %8s", triangles);
  ImGui::Text("Entities  %8u", stats.entities);
  ImGui::Text("Viewport  %ux%u", static_cast<unsigned>(stats.viewport_w),
              static_cast<unsigned>(stats.viewport_h));
  ImGui::Text("Cam speed %8.1f", camera_speed);

  IMComponents::EndOverlay(kPadding);
  ImGui::PopStyleVar();
  ImGui::PopFont();

  const ImVec2 content = ImGui::GetItemRectSize();
  g_size = ImVec2(content.x + kPadding.x * 2.0f, content.y + kPadding.y * 2.0f);
}

}  // namespace Viewport