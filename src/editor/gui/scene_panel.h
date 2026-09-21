#ifndef SCENE_PANEL_H
#define SCENE_PANEL_H

#include "window_base.h"
#include "world_settings.h"  // TODO: Panels::WorldSettings - rename to scene_settings.h later

class ScenePanel : public WindowBase {
public:
  void Render() override {
    ImGui::Begin("Scene", nullptr, EditorFlag::standard);
    Panels::WorldSettings();
    ImGui::End();
  }
};

#endif  // SCENE_PANEL_H