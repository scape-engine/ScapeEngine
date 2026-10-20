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
};

#endif  // CONSOLE_WINDOW_H