#include "editor_styles.h"

#include "editor/vendor/imguizmo/ImGuizmo.h"
#include "platform/paths.h"

namespace {
inline ImVec4 C(ImU32 c) {
  return ImGui::ColorConvertU32ToFloat4(c);
}
inline ImVec4 C(ImU32 c, float alpha) {
  ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
  v.w = alpha;
  return v;
}
}  // namespace

namespace EditorStyles {

// Font internals
Fonts g_fonts;

static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};

// Material Design Icons
// ? When using a new ICON_MDI_*, add it here
static const char* used_mdi_icons[] = {
    ICON_MDI_PLUS,
};

// Glyph ranges built from the list above. Static: ImGui reads the ranges
// when the atlas is built, so they must outlive LoadFonts.
static const ImWchar* MdiGlyphRanges() {
  static ImVector<ImWchar> ranges;
  if (ranges.empty()) {
    ImFontGlyphRangesBuilder builder;
    for (const char* icon : used_mdi_icons)
      builder.AddText(icon);
    builder.BuildRanges(&ranges);
  }
  return ranges.Data;
}

// Access fonts
const Fonts& GetFonts() {
  return g_fonts;
}

void _MergeIcons(ImGuiIO& io, float icon_size) {
  ImFontConfig icons_config;
  icons_config.MergeMode = true;
  icons_config.PixelSnapH = true;
  icons_config.RasterizerDensity = io.DisplayFramebufferScale.x;

  // FontAwesome: glyphs fill icon's em box 2/3
  const float fa_size = icon_size * 2.0f / 3.0f;
  icons_config.GlyphOffset = ImVec2(0.0f, -2.0f);
  icons_config.GlyphMinAdvanceX = fa_size;
  io.Fonts->AddFontFromFileTTF(
      Platform::ResolvePath(EditorFontPath::icons).c_str(), fa_size,
      &icons_config, icon_ranges);

  // MDI glyphs: padded 24px grid
  const float mdi_size = icon_size;
  icons_config.GlyphOffset = ImVec2(0.0f, 0.0f);
  icons_config.GlyphMinAdvanceX = mdi_size;
  io.Fonts->AddFontFromFileTTF(
      Platform::ResolvePath(EditorFontPath::icons_mdi).c_str(), mdi_size,
      &icons_config, MdiGlyphRanges());
}

void LoadFonts(ImGuiIO& io) {

  // Resolve font paths against the executable directory
  auto load_font = [&io](const char* relative, float size,
                         const ImFontConfig* cfg) -> ImFont* {
    return io.Fonts->AddFontFromFileTTF(Platform::ResolvePath(relative).c_str(),
                                        size, cfg);
  };

  // Rasterize at the display's real resolution
  const float density = io.DisplayFramebufferScale.x;

  // Global font config
  ImFontConfig main_config;
  main_config.MergeMode = false;
  main_config.PixelSnapH = true;
  main_config.OversampleH = 3;
  main_config.OversampleV = 2;
  main_config.RasterizerDensity = density;

  // p (paragraph)
  g_fonts.p = load_font(EditorFontPath::regular, EditorSizes::p_font_size,
                        &main_config);
  _MergeIcons(io, EditorSizes::p_icon_size);
  g_fonts.p_bold =
      load_font(EditorFontPath::bold, EditorSizes::p_font_size, &main_config);
  _MergeIcons(io, EditorSizes::p_bold_icon_size);

  // h1
  g_fonts.h1 = load_font(EditorFontPath::regular, EditorSizes::h1_font_size,
                         &main_config);
  g_fonts.h1_bold =
      load_font(EditorFontPath::bold, EditorSizes::h1_font_size, &main_config);

  // h2
  g_fonts.h2 = load_font(EditorFontPath::regular, EditorSizes::h2_font_size,
                         &main_config);
  _MergeIcons(io, EditorSizes::h2_icon_size);
  g_fonts.h2_bold =
      load_font(EditorFontPath::bold, EditorSizes::h2_font_size, &main_config);
  _MergeIcons(io, EditorSizes::h2_bold_icon_size);

  // h3
  g_fonts.h3 = load_font(EditorFontPath::regular, EditorSizes::h3_font_size,
                         &main_config);
  _MergeIcons(io, EditorSizes::h2_icon_size);
  g_fonts.h3_bold =
      load_font(EditorFontPath::bold, EditorSizes::h3_font_size, &main_config);
  _MergeIcons(io, EditorSizes::h2_icon_size);

  // h4
  g_fonts.h4 = load_font(EditorFontPath::regular, EditorSizes::h4_font_size,
                         &main_config);
  _MergeIcons(io, EditorSizes::p_bold_icon_size);
  g_fonts.h4_bold =
      load_font(EditorFontPath::bold, EditorSizes::h4_font_size, &main_config);
  _MergeIcons(io, EditorSizes::p_bold_icon_size);

  // s (small)
  g_fonts.s = load_font(EditorFontPath::regular, EditorSizes::s_font_size,
                        &main_config);
  _MergeIcons(io, EditorSizes::s_icon_size);
  g_fonts.s_bold =
      load_font(EditorFontPath::bold, EditorSizes::s_font_size, &main_config);
  _MergeIcons(io, EditorSizes::s_bold_icon_size);

  // console font config
  ImFontConfig console_config;
  console_config.OversampleH = 1;
  console_config.OversampleV = 1;
  console_config.PixelSnapH = true;
  console_config.RasterizerMultiply = 1.0f;
  console_config.RasterizerDensity = density;

  // load font for console and toolbar
  g_fonts.console = load_font(EditorFontPath::console,
                              EditorSizes::console_font_size, &console_config);

  g_fonts.toolbar = load_font(EditorFontPath::regular, EditorSizes::p_font_size,
                              &main_config);
  _MergeIcons(io, EditorSizes::toolbar_icon_size);
}

void SetupStyle() {
  ImGuiStyle& style = ImGui::GetStyle();
  ImVec4* colors = style.Colors;
  using EC = EditorColor;

  // === Surfaces ===
  colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_PopupBg] = C(EditorColor::surface_2, 0.98f);
  colors[ImGuiCol_MenuBarBg] = C(EditorColor::surface_2);

  // === Text ===
  colors[ImGuiCol_Text] = C(EditorColor::text);
  colors[ImGuiCol_TextDisabled] = C(EditorColor::text_disabled);
  colors[ImGuiCol_TextSelectedBg] = C(EditorColor::accent_deep);

  // === Borders ===
  colors[ImGuiCol_Border] = C(EditorColor::surface_0);
  colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

  // === Headers ===
  colors[ImGuiCol_Header] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_HeaderHovered] = C(EditorColor::hover_overlay);
  colors[ImGuiCol_HeaderActive] = C(EditorColor::orange_row);

  // === Inputs ===
  colors[ImGuiCol_FrameBg] = C(EditorColor::surface_1);
  colors[ImGuiCol_FrameBgHovered] = C(EditorColor::surface_4);
  colors[ImGuiCol_FrameBgActive] = C(EditorColor::surface_4);

  // === Buttons ===
  colors[ImGuiCol_Button] = C(EditorColor::surface_4);
  colors[ImGuiCol_ButtonHovered] = C(EditorColor::surface_5);
  colors[ImGuiCol_ButtonActive] = C(EditorColor::accent);

  // === Tabs ===
  colors[ImGuiCol_Tab] = C(EditorColor::surface_0);
  colors[ImGuiCol_TabHovered] = C(EditorColor::surface_5);
  colors[ImGuiCol_TabSelected] = C(EditorColor::surface_4);
  colors[ImGuiCol_TabSelectedOverline] = C(EditorColor::accent_border);
  colors[ImGuiCol_TabDimmedSelectedOverline] =
      C(EditorColor::accent_border, 0.5f);
  colors[ImGuiCol_TabDimmed] = C(EditorColor::surface_0);
  colors[ImGuiCol_TabDimmedSelected] = C(EditorColor::surface_4);

  // === Title bars ===
  colors[ImGuiCol_TitleBg] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0, 0, 0, 0);

  // === Separators ===
  colors[ImGuiCol_Separator] = C(EditorColor::surface_0);
  colors[ImGuiCol_SeparatorHovered] = C(EditorColor::accent, 0.6f);
  colors[ImGuiCol_SeparatorActive] = C(EditorColor::accent);

  // === Scrollbars ===
  colors[ImGuiCol_ScrollbarBg] = C(EditorColor::surface_1);
  colors[ImGuiCol_ScrollbarGrab] = C(EditorColor::surface_4);
  colors[ImGuiCol_ScrollbarGrabHovered] = C(EditorColor::line_strong);
  colors[ImGuiCol_ScrollbarGrabActive] = C(EditorColor::accent);

  // === Widgets ===
  colors[ImGuiCol_CheckMark] = C(EditorColor::accent);
  colors[ImGuiCol_SliderGrab] = C(EditorColor::accent);
  colors[ImGuiCol_SliderGrabActive] = C(EditorColor::accent_hover);
  colors[ImGuiCol_PlotLines] = C(EditorColor::accent);
  colors[ImGuiCol_PlotHistogram] = C(EditorColor::accent);
  colors[ImGuiCol_DragDropTarget] = C(EditorColor::accent_border);
  colors[ImGuiCol_NavCursor] = C(EditorColor::accent_border);
  colors[ImGuiCol_NavWindowingHighlight] = C(EditorColor::accent);
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.5f);

  // === Tables ===
  colors[ImGuiCol_TableHeaderBg] = C(EditorColor::surface_1);
  colors[ImGuiCol_TableBorderStrong] = C(EditorColor::line_strong);
  colors[ImGuiCol_TableBorderLight] = C(EditorColor::line_subtle);
  colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_TableRowBgAlt] = C(EditorColor::hover_overlay);

  // === Resize / docking ===
  colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_ResizeGripHovered] = C(EditorColor::accent);
  colors[ImGuiCol_ResizeGripActive] = C(EditorColor::accent);
  colors[ImGuiCol_DockingPreview] = C(EditorColor::accent, 0.40f);
  colors[ImGuiCol_DockingEmptyBg] = ImVec4(0, 0, 0, 0);

  // === Style vars ===
  style.FrameBorderSize = 0.0f;  // control outline (#424242)
  style.WindowBorderSize = 0.0f;
  style.ChildBorderSize = 0.0f;
  style.PopupBorderSize = 1.0f;
  style.TabBorderSize = 0.0f;
  style.TabBarOverlineSize = 1.0f;  // selected-tab top line

  style.WindowRounding =
      EditorSizes::panel_radius;  // floating windows / popups
  style.ChildRounding = EditorSizes::control_radius;
  style.FrameRounding = EditorSizes::control_radius;
  style.PopupRounding = EditorSizes::control_radius;
  style.TabRounding = 2.0f;
  style.GrabRounding = EditorSizes::control_radius;
  style.ScrollbarRounding = EditorSizes::control_radius;

  style.WindowPadding =
      ImVec2(EditorSizes::window_padding, EditorSizes::window_padding);
  style.FramePadding = ImVec2(5.0f, 2.0f);
  style.ItemSpacing =
      ImVec2(EditorSizes::item_spacing, EditorSizes::item_spacing);
  style.ItemInnerSpacing =
      ImVec2(EditorSizes::inner_spacing, EditorSizes::inner_spacing);
  style.CellPadding = ImVec2(5.0f, 2.0f);
  style.ScrollbarSize = 10.0f;
  style.GrabMinSize = 10.0f;

#ifdef IMGUI_HAS_DOCK
  style.DockingSeparatorSize =
      EditorSizes::panel_gap;  // 10px void gap between cards
  style.WindowMenuButtonPosition = ImGuiDir_None;
  style.TabBarOverlineSize = 1.0f;  // mockup has no overline
#endif
}

void SetupGizmoStyle() {
  ImVec4* colors = ImGuizmo::GetStyle().Colors;

  // Axes match the inspector's XYZ fields
  colors[ImGuizmo::DIRECTION_X] = C(EditorColor::axis_x);
  colors[ImGuizmo::DIRECTION_Y] = C(EditorColor::axis_y);
  colors[ImGuizmo::DIRECTION_Z] = C(EditorColor::axis_z);

  // Plane handles: same hues, translucent (ImGuizmo's default alpha)
  colors[ImGuizmo::PLANE_X] = C(EditorColor::axis_x, 0.38f);
  colors[ImGuizmo::PLANE_Y] = C(EditorColor::axis_y, 0.38f);
  colors[ImGuizmo::PLANE_Z] = C(EditorColor::axis_z, 0.38f);

  // Hovered / dragged handle
  colors[ImGuizmo::SELECTION] = C(EditorColor::orange);
  colors[ImGuizmo::ROTATION_USING_BORDER] = C(EditorColor::orange);
  colors[ImGuizmo::ROTATION_USING_FILL] = C(EditorColor::orange, 0.5f);

  colors[ImGuizmo::TEXT] = C(EditorColor::text);
}

void Initialize() {
  ImGuiIO& io = ImGui::GetIO();
  LoadFonts(io);
  SetupStyle();
  SetupGizmoStyle();
}

}  // namespace EditorStyles
