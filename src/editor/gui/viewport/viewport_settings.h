#ifndef VIEWPORT_SETTINGS_H
#define VIEWPORT_SETTINGS_H

class SceneViewPipeline;

namespace Viewport {

// How the scene is shaded. Only Lit and Wireframe exist in the renderer yet;
// the rest are shown disabled in the Views menu as a roadmap
enum class ViewMode { Lit, Wireframe, Unlit, Normals, Depth };

struct GridSettings {
  bool enabled = true;
  float cell_size = 1.0f;     // world units between minor lines
  int major_every = 10;       // a major line every N cells
  float fade_start = 200.0f;  // world units from the camera
  float fade_end = 500.0f;
  float opacity = 1.0f;
  bool axis_lines = true;
};

struct GizmoSettings {
  bool enabled = true;
  bool selection_outline = true;

  // TODO: disabled in the menu until the renderer supports them
  bool selection_wireframe = false;

  float billboard_opacity = 0.75f;
  float billboard_size = 0.5f;
  bool billboard_depth_aware = false;
  bool billboard_camera = true;
  bool billboard_light = true;

  bool camera_gizmos = true;
  bool model_gizmos = true;
  bool light_gizmos = true;
  bool volume_gizmos = true;
};

struct OverlaySettings {
  bool stats = false;
  bool notifications = true;
  bool axis_gizmo = true;
};

// Everything the scene view's overlay menus control.
// TODO: persist between launches
struct ViewportSettings {
  ViewMode view_mode = ViewMode::Lit;
  GridSettings grid;
  GizmoSettings gizmos;
  OverlaySettings overlays;
};

// Push the settings the renderer understands into the pipeline (each frame)
void ApplySettings(const ViewportSettings& settings,
                   SceneViewPipeline& pipeline);

}  // namespace Viewport

#endif  // VIEWPORT_SETTINGS_H