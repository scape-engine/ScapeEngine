#ifndef EDITOR_STYLES_H
#define EDITOR_STYLES_H

#include "../../vendor/IconFontCppHeaders/IconsFontAwesome6.h"

#include "imgui.h"

namespace EditorStyles {

struct Fonts {
  // Paragraph
  ImFont* p = nullptr;
  ImFont* p_bold = nullptr;

  // Headings
  ImFont* h1 = nullptr;
  ImFont* h1_bold = nullptr;
  ImFont* h2 = nullptr;
  ImFont* h2_bold = nullptr;
  ImFont* h3 = nullptr;
  ImFont* h3_bold = nullptr;
  ImFont* h4 = nullptr;
  ImFont* h4_bold = nullptr;

  // Small
  ImFont* s = nullptr;
  ImFont* s_bold = nullptr;

  // Special
  ImFont* console = nullptr;
  ImFont* icons = nullptr;
};

// Access fonts
const Fonts& GetFonts();

void LoadFonts(ImGuiIO& io);
void SetupStyle();
void Initialize();

}  // namespace EditorStyles

// FONT PATHS
struct EditorFontPath {
  // Switch from Monospace to a clean Sans-Serif font (Inter)
  static constexpr const char* regular = "resources/fonts/Inter-Regular.ttf";
  static constexpr const char* bold = "resources/fonts/Inter-SemiBold.ttf";
  static constexpr const char* icons = "resources/fonts/fa-solid-900.ttf";

  // Console font changed to FiraCode for better readability
  static constexpr const char* console = "resources/fonts/FiraCode-Regular.ttf";
};

// DRAW LIST COLOR CONSTANTS
struct EditorColor {
  // #181818 - Main Window Background (Primary)
  static constexpr ImU32 background = IM_COL32(24, 24, 24, 255);

  // #D9D9D9 - Standard text
  static constexpr ImU32 text = IM_COL32(217, 217, 217, 255);
  // #D9D9D9B4 - Transparent text
  static constexpr ImU32 text_transparent = IM_COL32(217, 217, 217, 180);

  // #21212182 - Transparent elements
  static constexpr ImU32 element_transparent = IM_COL32(33, 33, 33, 130);
  static constexpr ImU32 element_hovered_transparent_overlay =
      IM_COL32(255, 255, 255, 12);
  static constexpr ImU32 element_active_transparent_overlay =
      IM_COL32(255, 255, 255, 30);

  // #4D5C74 - Selection Highlight (Slate Blue Accent)
  static constexpr ImU32 selection = IM_COL32(77, 92, 116, 255);
  // #353535 - Inactive selection
  static constexpr ImU32 selection_inactive = IM_COL32(53, 53, 53, 255);

  // #212121 - Custom interactive element (Auxiliary)
  static constexpr ImU32 element = IM_COL32(33, 33, 33, 255);
  // #2A2A2A - Hovered
  static constexpr ImU32 element_hovered = IM_COL32(42, 42, 42, 255);
  // #4D5C74 - Active (Slate Blue Accent)
  static constexpr ImU32 element_active = IM_COL32(77, 92, 116, 255);
  // #181818 - Component header
  static constexpr ImU32 element_component = IM_COL32(24, 24, 24, 255);

  // #0F0F0F - Standard dark border (Void)
  static constexpr ImU32 border_color = IM_COL32(15, 15, 15, 255);
  // #0F0F0F - Tab background
  static constexpr ImU32 tab_color = IM_COL32(15, 15, 15, 255);
};

// SIZING CONSTANTS
struct EditorSizes {
  // Font sizes
  static constexpr float p_font_size =
      15.0f;  // Bumped to 15 for better readability
  static constexpr float h1_font_size =
      24.0f;  // Smoothed out heading hierarchy
  static constexpr float h2_font_size = 20.0f;
  static constexpr float h3_font_size = 17.0f;
  static constexpr float h4_font_size = 15.0f;
  static constexpr float s_font_size = 13.0f;
  static constexpr float console_font_size = 13.0f;  // Bumped slightly

  // Icon sizes (Scaled to match the new text baseline, prevents UI stretching)
  static constexpr float p_icon_size =
      16.0f;  // Reduced from 22 to align with 15px text
  static constexpr float p_bold_icon_size = 16.0f;
  static constexpr float h2_icon_size = 22.0f;
  static constexpr float h2_bold_icon_size = 22.0f;
  static constexpr float s_icon_size = 14.0f;
  static constexpr float s_bold_icon_size = 14.0f;

  // Layout
  static constexpr float window_padding = 8.0f;  // Reduced from 30.0f
  static constexpr float frame_padding = 4.0f;
};

struct EditorFlag {
  static constexpr ImGuiWindowFlags standard =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
  static constexpr ImGuiWindowFlags fixed = ImGuiWindowFlags_NoResize |
                                            ImGuiWindowFlags_AlwaysAutoResize |
                                            ImGuiWindowFlags_NoTitleBar;
};

#endif  // EDITOR_STYLES_H