#include "viewport_settings.h"

#include "editor/gui/styles/editor_styles.h"
#include "editor/pipelines/scene_view_pipeline.h"

#include <imgui.h>

namespace {

glm::vec3 ToVec3(ImU32 color) {
  const ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
  return glm::vec3(c.x, c.y, c.z);
}

// Grid settings + theme colors, as pipeline's grid parameters
GridParams ToGridParams(const Viewport::GridSettings& settings) {
  GridParams grid;
  grid.cell_size = settings.cell_size;
  grid.major_every = settings.major_every;
  grid.fade_start = settings.fade_start;
  grid.fade_end = settings.fade_end;
  grid.opacity = settings.opacity;
  grid.axis_lines = settings.axis_lines;
  grid.axis_x_color = ToVec3(EditorColor::axis_x);
  grid.axis_z_color = ToVec3(EditorColor::axis_z);
  return grid;
}

}  // namespace

namespace Viewport {

void ApplySettings(const ViewportSettings& settings,
                   SceneViewPipeline& pipeline) {
  pipeline.SetShowGrid(settings.grid.enabled);
  pipeline.SetGridParams(ToGridParams(settings.grid));
  pipeline.SetShowGizmos(settings.gizmos.enabled);
  pipeline.SetShowSelectionOutline(settings.gizmos.selection_outline);
  pipeline.SetWireframe(settings.view_mode == ViewMode::Wireframe);
}

}  // namespace Viewport