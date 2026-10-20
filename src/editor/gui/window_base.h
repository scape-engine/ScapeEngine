#ifndef WINDOW_BASE_H
#define WINDOW_BASE_H

#define IMGUI_DEFINE_MATH_OPERATORS
#include <cstdint>
#include <glm/glm.hpp>
#include "imgui.h"

#include "editor/gui/components/im_components.h"
#include "editor/gui/inspectables/inspectable_base.h"
#include "editor/gui/modals/popup_menu.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/gui/utils/gui_utils.h"
#include "editor/runtime/runtime.h"
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"

#include "engine/time/time.h"

class WindowBase {

public:
  virtual ~WindowBase() = default;
  virtual void Render() = 0;
};

#endif  // WINDOW_BASE_H