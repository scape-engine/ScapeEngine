#include "gizmos_menu.h"

#include "editor/gui/components/im_components.h"

namespace Viewport {

// TODO: Reflection probes and particle emitters
void GizmosMenu(GizmoSettings& settings) {
  // Same setting as the toolbar icon, here for discoverability
  IMComponents::PopupCheckbox("Show Gizmos", &settings.enabled);

  IMComponents::PopupSection("Selection");
  IMComponents::PopupCheckbox("Selection Outline", &settings.selection_outline);
  IMComponents::PopupCheckbox("Selection Wireframe",
                              &settings.selection_wireframe, false);

  IMComponents::PopupSection("Billboards");
  IMComponents::PopupSlider("Opacity", &settings.billboard_opacity, 0.0f, 1.0f,
                            "%.2f");
  IMComponents::PopupSlider("Size", &settings.billboard_size, 0.1f, 2.0f,
                            "%.2f");
  IMComponents::PopupCheckbox("Depth Aware", &settings.billboard_depth_aware);

  // Roadmap: needs a category per scene icon in ComponentRegistry
  IMComponents::PopupCheckbox("Camera", &settings.billboard_camera, false);
  IMComponents::PopupCheckbox("Light", &settings.billboard_light, false);

  // Roadmap: needs ComponentGizmos shape drawing (frustums, ranges, bounds)
  IMComponents::PopupSection("Component Gizmos");
  IMComponents::PopupCheckbox("Camera Gizmos", &settings.camera_gizmos, false);
  IMComponents::PopupCheckbox("Model Gizmos", &settings.model_gizmos, false);
  IMComponents::PopupCheckbox("Light Gizmos", &settings.light_gizmos, false);
  IMComponents::PopupCheckbox("Sound Gizmos", &settings.sound_gizmos, false);
}

}  // namespace Viewport