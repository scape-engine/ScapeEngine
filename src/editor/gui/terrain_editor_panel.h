#ifndef TERRAIN_EDITOR_PANEL_H
#define TERRAIN_EDITOR_PANEL_H

#include "terrain_editor.h"
#include "window_base.h"

class TerrainEditorPanel : public WindowBase {
public:
  void Render() override {
    ImGui::Begin("Terrain Editor", nullptr, EditorFlag::standard);
    Panels::TerrainEditor();
    ImGui::End();
  }
};

#endif  // TERRAIN_EDITOR_PANEL_H