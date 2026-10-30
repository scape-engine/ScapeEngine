#include "views_menu.h"

#include <imgui.h>
#include <string>

#include "editor/gui/components/im_components.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/gui/viewport/action_overlay.h"
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"

namespace Viewport {

namespace {

struct ViewModeInfo {
  ViewMode mode;
  const char* icon;
  const char* name;
  const char* description;
  bool implemented;
};

// Display order of the Views tab. Roadmap entries are shown disabled
const ViewModeInfo view_modes[] = {
    {ViewMode::Lit, ICON_FA_SUN, "Lit",
     "The shaded scene, as the game renders it.", true},
    {ViewMode::Wireframe, ICON_FA_DRAW_POLYGON, "Wireframe",
     "Mesh edges only, to inspect topology and density.", true},
    {ViewMode::Unlit, ICON_FA_CIRCLE_HALF_STROKE, "Unlit",
     "Base colors without lighting.", false},
    {ViewMode::Normals, ICON_FA_COMPASS, "Normals",
     "Surface normals shown as colors.", false},
    {ViewMode::Depth, ICON_FA_LAYER_GROUP, "Depth",
     "Distance from the camera, shown in grayscale.", false},
};

void ViewsTab(ViewportSettings& settings) {
  const ViewModeInfo* current = &view_modes[0];

  for (const ViewModeInfo& info : view_modes) {
    const bool selected = settings.view_mode == info.mode;
    if (selected)
      current = &info;
    if (IMComponents::PopupOption(info.icon, info.name, selected,
                                  info.implemented)) {
      settings.view_mode = info.mode;
      PostAction(std::string("View: ") + info.name);
    }
  }

  // Description of the active mode
  IMComponents::PopupSection(current->name);
  ImGui::PushStyleColor(ImGuiCol_Text, EditorColor::text_dim);
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() +
                         ImGui::GetContentRegionAvail().x);
  ImGui::TextUnformatted(current->description);
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
}

void OverlaysTab(OverlaySettings& overlays) {
  IMComponents::PopupCheckbox("Stats (F3)", &overlays.stats);
  IMComponents::PopupCheckbox("Notifications", &overlays.notifications);
  IMComponents::PopupCheckbox("Axis Gizmo", &overlays.axis_gizmo);
}

}  // namespace

void ViewsMenu(ViewportSettings& settings) {
  if (!ImGui::BeginTabBar("##ViewsTabs"))
    return;

  if (ImGui::BeginTabItem("Views")) {
    ViewsTab(settings);
    ImGui::EndTabItem();
  }
  if (ImGui::BeginTabItem("Overlays")) {
    OverlaysTab(settings.overlays);
    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
}

}  // namespace Viewport