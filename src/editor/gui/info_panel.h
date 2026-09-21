#ifndef INFO_PANEL_H
#define INFO_PANEL_H

#include "window_base.h"

//---------------------------------------------------------------------------
// INFO PANEL
// - Read-only overview of the open project (.project metadata, assets, config)
//---------------------------------------------------------------------------
class InfoPanel : public WindowBase {
public:
  void Render() override;

private:
  void RenderMetadata();
  void RenderAssets();
  void RenderConfiguration();
  void RenderLog();

  static constexpr float pad_x_ = 24.0f;
  static constexpr float pad_y_ = 14.0f;
  static constexpr float line_gap_ = 6.0f;
};

#endif  // INFO_PANEL_H