#ifndef ASSET_BROWSER_WINDOW_H
#define ASSET_BROWSER_WINDOW_H

#include <imgui.h>

#include "editor/gui/asset_browser.h"
#include "editor/gui/utils/gui_utils.h"
#include "editor/gui/window_base.h"

class AssetBrowserWindow : public WindowBase {
public:
  void Render() override {
    GUIUtils::HideDockTabBar();
    ImGui::Begin("Asset Browser", nullptr);
    Panels::AssetBrowser();
    ImGui::End();
  }
};

#endif  // ASSET_BROWSER_WINDOW_H