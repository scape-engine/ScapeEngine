#ifndef CONSOLE_WINDOW_H
#define CONSOLE_WINDOW_H

#include <imgui.h>

#include "editor/gui/console_panel.h"
#include "editor/gui/utils/gui_utils.h"
#include "editor/gui/window_base.h"

class ConsoleWindow : public WindowBase {
public:
  void Render() override {
    GUIUtils::HideDockTabBar();
    ImGui::Begin("Console", nullptr);
    Panels::ConsolePanel();
    ImGui::End();
  }

  // Errors logged since the console was last visible
  uint32_t UnseenErrors() const {
    return static_cast<uint32_t>(Logger::getInstance().GetErrorCount() -
                                 seen_errors_);
  }

private:
  uint32_t seen_errors_ = 0;
};

#endif  // CONSOLE_WINDOW_H