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

struct EditorColor {
  // ---- Surfaces ----
  static constexpr ImU32 void_bg =
      IM_COL32(15, 15, 15, 255);  // #0F0F0F app bg behind cards
  static constexpr ImU32 panel =
      IM_COL32(24, 24, 24, 255);  // #181818 panel cards / bars
  static constexpr ImU32 panel_stroke =
      IM_COL32(40, 40, 40, 255);  // #303030 hairline under headers
  static constexpr ImU32 strip =
      IM_COL32(19, 19, 19, 255);  // #131313 rulers / list strips
  static constexpr ImU32 recessed =
      IM_COL32(14, 14, 14, 255);  // #0E0E0E sunken zones
  static constexpr ImU32 input_bg =
      IM_COL32(21, 21, 21, 255);  // #151515 text fields
  static constexpr ImU32 control =
      IM_COL32(48, 48, 48, 255);  // #303030 buttons / dropdowns
  static constexpr ImU32 control_hovered =
      IM_COL32(58, 58, 58, 255);  // #3A3A3A
  static constexpr ImU32 control_border =
      IM_COL32(66, 66, 66, 255);  // #424242 1px outline / separators
  static constexpr ImU32 control_selected =
      IM_COL32(49, 49, 49, 255);  // #313131 active tab / rail item
  static constexpr ImU32 grid_minor = IM_COL32(45, 45, 45, 255);  // #2D2D2D
  static constexpr ImU32 grid_major = IM_COL32(61, 61, 61, 255);  // #3D3D3D
  static constexpr ImU32 selection_row =
      IM_COL32(49, 105, 227, 64);  // #3169E3 @ 0.25 row fill
  static constexpr ImU32 tree_guide =
      IM_COL32(211, 211, 211, 153);  // #D3D3D3 @ 0.6

  // ---- Text ----
  static constexpr ImU32 text = IM_COL32(211, 211, 211, 255);      // #D3D3D3
  static constexpr ImU32 text_dim = IM_COL32(188, 188, 188, 255);  // #BCBCBC
  static constexpr ImU32 text_bright = IM_COL32(255, 255, 255, 255);
  static constexpr ImU32 text_placeholder =
      IM_COL32(255, 255, 255, 102);  // white @ 0.40
  static constexpr ImU32 text_disabled = IM_COL32(211, 211, 211, 128);

  // ---- Accents ----
  static constexpr ImU32 accent =
      IM_COL32(49, 105, 227, 255);  // #3169E3 toggled / playhead
  static constexpr ImU32 accent_border =
      IM_COL32(91, 142, 255, 255);                                    // #5B8EFF
  static constexpr ImU32 accent_hover = IM_COL32(82, 131, 232, 255);  // #5283E8
  static constexpr ImU32 accent_deep =
      IM_COL32(38, 87, 135, 255);  // #265787 selection fill
  static constexpr ImU32 orange =
      IM_COL32(225, 150, 88, 255);  // #E19658 active object
  static constexpr ImU32 orange_dim =
      IM_COL32(192, 132, 83, 255);  // #C08453 selected text
  static constexpr ImU32 orange_row =
      IM_COL32(192, 132, 83, 51);  // #C08453 @ 0.20 row bg
  static constexpr ImU32 success = IM_COL32(13, 182, 143, 255);  // #0DB68F
  static constexpr ImU32 error = IM_COL32(171, 60, 72, 255);     // #AB3C48
  static constexpr ImU32 warning = IM_COL32(234, 118, 0, 255);   // #EA7600

  static constexpr ImU32 axis_x = IM_COL32(245, 53, 81, 255);   // #F53551
  static constexpr ImU32 axis_y = IM_COL32(105, 157, 21, 255);  // #699D15
  static constexpr ImU32 axis_z = IM_COL32(96, 121, 255, 255);  // #6079FF

  // ---- Overlays ----
  static constexpr ImU32 hover_overlay = IM_COL32(255, 255, 255, 12);
  static constexpr ImU32 active_overlay = IM_COL32(255, 255, 255, 30);

  // ---- Legacy aliases (existing call sites; retire during the layout pass)
  // ----
  static constexpr ImU32 background = panel;
  static constexpr ImU32 text_transparent = IM_COL32(211, 211, 211, 153);
  static constexpr ImU32 element_transparent = IM_COL32(32, 32, 32, 130);
  static constexpr ImU32 element_hovered_transparent_overlay = hover_overlay;
  static constexpr ImU32 element_active_transparent_overlay = active_overlay;
  static constexpr ImU32 selection = accent_deep;
  static constexpr ImU32 selection_inactive = control_selected;
  static constexpr ImU32 element = control;
  static constexpr ImU32 element_hovered = control_hovered;
  static constexpr ImU32 element_active = accent;
  static constexpr ImU32 element_component = strip;
  static constexpr ImU32 border_color = control_border;
  static constexpr ImU32 tab_color = void_bg;
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
  static constexpr float window_padding = 10.0f;  // Reduced from 30.0f
  static constexpr float frame_padding = 4.0f;

  static constexpr float panel_radius = 8.0f;
  static constexpr float control_radius = 4.0f;
  static constexpr float panel_gap = 10.0f;     // gap between docked panels
  static constexpr float panel_margin = 10.0f;  // outer margin around dockspace
  static constexpr float item_spacing = 5.0f;
  static constexpr float inner_spacing = 2.0f;
  static constexpr float header_bar_height = 30.0f;  // panel header strip
  static constexpr float control_height = 20.0f;     // toolbar buttons / inputs
  static constexpr float hairline = 1.0f;
};

struct EditorFlag {
  static constexpr ImGuiWindowFlags standard =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
  static constexpr ImGuiWindowFlags fixed = ImGuiWindowFlags_NoResize |
                                            ImGuiWindowFlags_AlwaysAutoResize |
                                            ImGuiWindowFlags_NoTitleBar;
};

#endif  // EDITOR_STYLES_H