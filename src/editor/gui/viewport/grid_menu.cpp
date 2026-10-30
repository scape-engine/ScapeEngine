#include "grid_menu.h"

#include <algorithm>

#include "editor/gui/components/im_components.h"
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"

namespace Viewport {

void GridMenu(GridSettings& settings) {
  // Same setting as the toolbar icon
  IMComponents::PopupCheckbox("Show Grid", &settings.enabled);

  IMComponents::PopupSection("Spacing");
  IMComponents::PopupSlider("Cell Size", &settings.cell_size, 0.1f, 10.0f,
                            "%.2f m", true, ImGuiSliderFlags_Logarithmic);
  IMComponents::PopupSlider("Major Lines Every", &settings.major_every, 2, 20);

  IMComponents::PopupSection("Appearance");
  IMComponents::PopupSlider("Opacity", &settings.opacity, 0.0f, 1.0f);
  IMComponents::PopupCheckbox("Axis Lines", &settings.axis_lines);

  IMComponents::PopupSection("Fade");
  IMComponents::PopupSlider("Fade Start", &settings.fade_start, 10.0f, 5000.0f,
                            "%.0f m", true, ImGuiSliderFlags_Logarithmic);
  IMComponents::PopupSlider("Fade End", &settings.fade_end, 10.0f, 5000.0f,
                            "%.0f m", true, ImGuiSliderFlags_Logarithmic);

  // Keep the fade range valid: end must be beyond start
  settings.fade_end = std::max(settings.fade_end, settings.fade_start + 1.0f);

  // Roadmap: grids on other planes need a rotated quad and shader axes
  IMComponents::PopupSection("Plane");
  IMComponents::PopupOption(ICON_FA_BORDER_ALL, "XZ (ground)", true, false);
  IMComponents::PopupOption(ICON_FA_BORDER_ALL, "XY", false, false);
  IMComponents::PopupOption(ICON_FA_BORDER_ALL, "YZ", false, false);
}

}  // namespace Viewport