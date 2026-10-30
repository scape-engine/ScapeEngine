#ifndef VIEWS_MENU_H
#define VIEWS_MENU_H

#include "editor/gui/viewport/viewport_settings.h"

namespace Viewport {

// Contents of the Views popup: shading mode (Views tab) and overlay
// visibility (Overlays tab)
void ViewsMenu(ViewportSettings& settings);

}  // namespace Viewport

#endif  // VIEWS_MENU_H