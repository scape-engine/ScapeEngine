#ifndef SCENE_MODALS_H
#define SCENE_MODALS_H

#include <functional>

// "Unsaved changes" and "Save scene as" dialogs
class SceneModals {
public:
  // Runs the action now, or once unsaved scene changes are saved or discarded
  static void RunAfterSaveCheck(std::function<void()> action);

  // Saves the open scene, asking for a path if it has none
  static void RequestSave();
  static void RequestSaveAs();

  // Draw the dialogs, once per frame
  static void Render();
};

#endif  // SCENE_MODALS_H
