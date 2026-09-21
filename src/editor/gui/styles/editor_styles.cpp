#include "editor_styles.h"

#include "platform/paths.h"

namespace {
inline ImVec4 C(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
inline ImVec4 C(ImU32 c, float alpha) {
  ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
  v.w = alpha;
  return v;
}
}  // namespace

namespace EditorStyles {

static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};

// Font internals
Fonts g_fonts;

// Access fonts
const Fonts& GetFonts() {
  return g_fonts;
}

void _MergeIcons(ImGuiIO& io, float icon_size) {
  float icons_font_size = icon_size * 2.0f / 3.0f;

  ImFontConfig icons_config;
  icons_config.MergeMode = true;
  icons_config.PixelSnapH = true;
  icons_config.GlyphMinAdvanceX = icons_font_size;

  g_fonts.icons = io.Fonts->AddFontFromFileTTF(
      Platform::ResolvePath(EditorFontPath::icons).c_str(), icons_font_size,
      &icons_config, icon_ranges);
}

void LoadFonts(ImGuiIO& io) {

  // Resolve font paths against the executable directory
  auto load_font = [&io](const char* relative, float size,
                         const ImFontConfig* cfg) -> ImFont* {
    return io.Fonts->AddFontFromFileTTF(Platform::ResolvePath(relative).c_str(),
                                        size, cfg);
  };

  // Global font config
  ImFontConfig main_config;
  main_config.MergeMode = false;
  main_config.PixelSnapH = true;
  main_config.OversampleH = 3;
  main_config.OversampleV = 2;

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

  // load font for console
  g_fonts.console = load_font(EditorFontPath::console,
                              EditorSizes::console_font_size, &console_config);
}

void SetupStyle() {
  ImGuiStyle& style = ImGui::GetStyle();
  ImVec4* colors = style.Colors;
  using EC = EditorColor;

  // === Surfaces ===
  colors[ImGuiCol_WindowBg]  = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_ChildBg]   = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_PopupBg]   = C(EC::panel, 0.98f);
  colors[ImGuiCol_MenuBarBg] = C(EC::panel);

  // === Text ===
  colors[ImGuiCol_Text]           = C(EC::text);
  colors[ImGuiCol_TextDisabled]   = C(EC::text_disabled);
  colors[ImGuiCol_TextSelectedBg] = C(EC::accent_deep);

  // === Borders (1px control outline via FrameBorderSize) ===
  colors[ImGuiCol_Border]       = C(EC::void_bg);
  colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

  // === Headers (tree rows, selectables, collapsing headers) ===
  colors[ImGuiCol_Header]        = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_HeaderHovered] = C(EC::hover_overlay);
  colors[ImGuiCol_HeaderActive]  = C(EC::orange_row);

  // === Inputs ===
  colors[ImGuiCol_FrameBg]        = C(EC::input_bg);
  colors[ImGuiCol_FrameBgHovered] = C(EC::control);
  colors[ImGuiCol_FrameBgActive]  = C(EC::control);

  // === Buttons ===
  colors[ImGuiCol_Button]        = C(EC::control);
  colors[ImGuiCol_ButtonHovered] = C(EC::control_hovered);
  colors[ImGuiCol_ButtonActive]  = C(EC::accent);

  // === Tabs (no accent overline in mockup) ===
  colors[ImGuiCol_Tab]                       = C(EC::void_bg);
  colors[ImGuiCol_TabHovered]                = C(EC::control_hovered);
  colors[ImGuiCol_TabSelected]               = C(EC::control_selected);
  colors[ImGuiCol_TabSelectedOverline]       = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_TabDimmed]                 = C(EC::void_bg);
  colors[ImGuiCol_TabDimmedSelected]         = C(EC::control_selected);
  colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0, 0, 0, 0);

  // === Title bars (docked tab-bar strip + floating windows) ===
  colors[ImGuiCol_TitleBg]          = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_TitleBgActive]    = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0, 0, 0, 0);

  // === Separators (dock gaps are painted by the host; these are in-panel) ===
  colors[ImGuiCol_Separator]        = C(EC::void_bg);
  colors[ImGuiCol_SeparatorHovered] = C(EC::accent, 0.6f);
  colors[ImGuiCol_SeparatorActive]  = C(EC::accent);

  // === Scrollbars ===
  colors[ImGuiCol_ScrollbarBg]          = C(EC::strip);
  colors[ImGuiCol_ScrollbarGrab]        = C(EC::control);
  colors[ImGuiCol_ScrollbarGrabHovered] = C(EC::control_border);
  colors[ImGuiCol_ScrollbarGrabActive]  = C(EC::accent);

  // === Widgets ===
  colors[ImGuiCol_CheckMark]        = C(EC::accent);
  colors[ImGuiCol_SliderGrab]       = C(EC::accent);
  colors[ImGuiCol_SliderGrabActive] = C(EC::accent_hover);
  colors[ImGuiCol_PlotLines]        = C(EC::accent);
  colors[ImGuiCol_PlotHistogram]    = C(EC::accent);
  colors[ImGuiCol_DragDropTarget]   = C(EC::accent_border);
  colors[ImGuiCol_NavCursor]        = C(EC::accent_border);
  colors[ImGuiCol_NavWindowingHighlight] = C(EC::accent);
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.5f);

  // === Tables ===
  colors[ImGuiCol_TableHeaderBg]     = C(EC::strip);
  colors[ImGuiCol_TableBorderStrong] = C(EC::control_border);
  colors[ImGuiCol_TableBorderLight]  = C(EC::panel_stroke);
  colors[ImGuiCol_TableRowBg]        = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_TableRowBgAlt]     = C(EC::hover_overlay);

  // === Resize / docking ===
  colors[ImGuiCol_ResizeGrip]        = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_ResizeGripHovered] = C(EC::accent);
  colors[ImGuiCol_ResizeGripActive]  = C(EC::accent);
  colors[ImGuiCol_DockingPreview]    = C(EC::accent, 0.40f);
  colors[ImGuiCol_DockingEmptyBg]   = ImVec4(0, 0, 0, 0);

  // === Style vars ===
  style.FrameBorderSize  = 1.0f;   // control outline (#424242)
  style.WindowBorderSize = 0.0f;
  style.ChildBorderSize  = 0.0f;
  style.PopupBorderSize  = 1.0f;
  style.TabBorderSize    = 0.0f;

  style.WindowRounding    = EditorSizes::panel_radius;    // floating windows / popups
  style.ChildRounding     = EditorSizes::control_radius;
  style.FrameRounding     = EditorSizes::control_radius;
  style.PopupRounding     = EditorSizes::control_radius;
  style.TabRounding       = EditorSizes::control_radius;
  style.GrabRounding      = EditorSizes::control_radius;
  style.ScrollbarRounding = EditorSizes::control_radius;

  style.WindowPadding    = ImVec2(EditorSizes::window_padding, EditorSizes::window_padding);
  style.FramePadding     = ImVec2(5.0f, 2.0f);
  style.ItemSpacing      = ImVec2(EditorSizes::item_spacing, EditorSizes::item_spacing);
  style.ItemInnerSpacing = ImVec2(EditorSizes::inner_spacing, EditorSizes::inner_spacing);
  style.CellPadding      = ImVec2(5.0f, 2.0f);
  style.ScrollbarSize    = 10.0f;
  style.GrabMinSize      = 10.0f;

#ifdef IMGUI_HAS_DOCK
  style.DockingSeparatorSize    = EditorSizes::panel_gap;  // 10px void gap between cards
  style.WindowMenuButtonPosition = ImGuiDir_None;
  style.TabBarOverlineSize       = 0.0f;                   // mockup has no overline
#endif
}

void Initialize() {
  ImGuiIO& io = ImGui::GetIO();
  LoadFonts(io);
  SetupStyle();
}

}  // namespace EditorStyles
