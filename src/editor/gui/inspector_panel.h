#ifndef INSPECTOR_PANEL_H
#define INSPECTOR_PANEL_H

#include <memory>
#include <optional>
#include <string>
#include <type_traits>

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

  // Output texture of preview viewer
  size_t preview_viewer_output_;

  // Current height of preview viewer
  float preview_viewer_height_;

  // Transform of preview viewer camera
  TransformComponent preview_camera_transform_;

  // Render the nothing is inspected text?
  void RenderNoneInspected();

  // Render preview based on given preview renderer
  void RenderPreviewViewer(ImDrawList& draw_list, ImVec2 size);
};

#endif  // INSPECTOR_PANEL_H