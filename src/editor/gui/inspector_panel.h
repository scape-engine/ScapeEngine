#ifndef INSPECTOR_PANEL_H
#define INSPECTOR_PANEL_H

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "editor/gui/asset_preview.h"
#include "editor/systems/asset_meta.h"
#include "window_base.h"

class InspectorPanel : public WindowBase {
public:
  InspectorPanel();

  void Render() override;

  template <typename T, typename... Args>
  static void Inspect(Args&&... args) {
    // Provided type should be derived from inspectable
    static_assert(std::is_base_of_v<InspectableBase, T>,
                  "Only objects which are derived from class 'Inspectable' can "
                  "be inspected!");

    // Instantiate new inspectable
    inspected_ = std::make_unique<T>(std::forward<Args>(args)...);
  }

  // void InspectVolume(Entity entity);

private:
  // Current inspected object
  static inline std::unique_ptr<InspectableBase> inspected_;

  Entity last_inspected_entity_ = entt::null;
  AssetSID last_inspected_asset_ = 0;

  // The preview under the inspector, and the same in its own window
  // (popped out); the inspected item they were last reset for
  AssetPreview preview_;
  AssetPreview viewer_;
  const InspectableBase* previewed_ = nullptr;
  bool preview_open_ = true;
  bool popped_out_ = false;
  // The preview's height under its header, set by dragging its top edge
  float preview_height_ = 300.0f;

  // Render the nothing is inspected text?
  void RenderNoneInspected();

  // The preview's header (folding it, its buttons, popping it out) and,
  // while open here, the preview itself
  void RenderPreview(ImDrawList& draw_list);
  // The preview in its own window, while popped out
  void RenderViewer();
};

#endif  // INSPECTOR_PANEL_H