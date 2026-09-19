#include "editor_styles.h"

#include "platform/paths.h"

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

  // === Core Layout (Strictly Neutral Greys) ===
  // Primary Shade: #181818
  // Auxiliary Shade: #212121
  // Void/Borders: #0F0F0F

  colors[ImGuiCol_WindowBg] =
      ImVec4(0.094f, 0.094f, 0.094f, 1.00f);  // #181818 (Primary)
  colors[ImGuiCol_ChildBg] = ImVec4(0.094f, 0.094f, 0.094f, 1.00f);  // #181818
  colors[ImGuiCol_PopupBg] =
      ImVec4(0.129f, 0.129f, 0.129f, 0.98f);  // #212121 (Auxiliary)
  colors[ImGuiCol_MenuBarBg] =
      ImVec4(0.059f, 0.059f, 0.059f, 1.00f);  // #0F0F0F (Void)

  // === Text & Icons ===
  colors[ImGuiCol_Text] = ImVec4(0.850f, 0.850f, 0.850f, 1.00f);  // #D9D9D9
  colors[ImGuiCol_TextDisabled] =
      ImVec4(0.400f, 0.400f, 0.400f, 1.00f);  // #666666

  // === Borders (Recessed trick: borders are darker than panels) ===
  colors[ImGuiCol_Border] = ImVec4(0.059f, 0.059f, 0.059f, 1.00f);  // #0F0F0F
  colors[ImGuiCol_BorderShadow] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);

  // === Headers (Trees, Inspectors, Expanders) ===
  colors[ImGuiCol_Header] = ImVec4(0.129f, 0.129f, 0.129f, 1.00f);  // #212121
  colors[ImGuiCol_HeaderHovered] =
      ImVec4(0.165f, 0.165f, 0.165f, 1.00f);  // #2A2A2A
  colors[ImGuiCol_HeaderActive] =
      ImVec4(0.302f, 0.361f, 0.455f, 1.00f);  // #4D5C74 (Slate Blue Accent)

  // === Frames / Inputs / Buttons (Auxiliary #212121) ===
  colors[ImGuiCol_FrameBg] = ImVec4(0.129f, 0.129f, 0.129f, 1.00f);  // #212121
  colors[ImGuiCol_FrameBgHovered] =
      ImVec4(0.165f, 0.165f, 0.165f, 1.00f);  // #2A2A2A
  colors[ImGuiCol_FrameBgActive] =
      ImVec4(0.302f, 0.361f, 0.455f, 1.00f);                        // #4D5C74
  colors[ImGuiCol_Button] = ImVec4(0.129f, 0.129f, 0.129f, 1.00f);  // #212121
  colors[ImGuiCol_ButtonHovered] =
      ImVec4(0.165f, 0.165f, 0.165f, 1.00f);  // #2A2A2A
  colors[ImGuiCol_ButtonActive] =
      ImVec4(0.302f, 0.361f, 0.455f, 1.00f);  // #4D5C74

  // === Tabs ===
  colors[ImGuiCol_Tab] =
      ImVec4(0.059f, 0.059f, 0.059f, 1.00f);  // #0F0F0F (Void)
  colors[ImGuiCol_TabHovered] =
      ImVec4(0.129f, 0.129f, 0.129f, 1.00f);  // #212121
  colors[ImGuiCol_TabActive] =
      ImVec4(0.094f, 0.094f, 0.094f, 1.00f);  // #181818 (Primary)
  colors[ImGuiCol_TabUnfocused] =
      ImVec4(0.059f, 0.059f, 0.059f, 1.00f);  // #0F0F0F
  colors[ImGuiCol_TabUnfocusedActive] =
      ImVec4(0.094f, 0.094f, 0.094f, 1.00f);  // #181818
  style.Colors[ImGuiCol_TabSelectedOverline] =
      ImVec4(0.302f, 0.361f, 0.455f, 1.00f);  // #4D5C74

  // === Title Bar ===
  colors[ImGuiCol_TitleBg] = ImVec4(0.059f, 0.059f, 0.059f, 1.00f);  // #0F0F0F
  colors[ImGuiCol_TitleBgActive] =
      ImVec4(0.059f, 0.059f, 0.059f, 1.00f);  // #0F0F0F
  colors[ImGuiCol_TitleBgCollapsed] =
      ImVec4(0.059f, 0.059f, 0.059f, 1.00f);  // #0F0F0F

  // === Separator ===
  colors[ImGuiCol_Separator] =
      ImVec4(0.059f, 0.059f, 0.059f, 1.00f);  // #0F0F0F
  colors[ImGuiCol_SeparatorHovered] =
      ImVec4(0.302f, 0.361f, 0.455f, 1.00f);  // #4D5C74
  colors[ImGuiCol_SeparatorActive] =
      ImVec4(0.302f, 0.361f, 0.455f, 1.00f);  // #4D5C74

  // === Scroller ===
  colors[ImGuiCol_ScrollbarBg] =
      ImVec4(0.059f, 0.059f, 0.059f, 1.00f);  // #0F0F0F
  colors[ImGuiCol_ScrollbarGrab] =
      ImVec4(0.129f, 0.129f, 0.129f, 1.00f);  // #212121
  colors[ImGuiCol_ScrollbarGrabHovered] =
      ImVec4(0.165f, 0.165f, 0.165f, 1.00f);  // #2A2A2A
  colors[ImGuiCol_ScrollbarGrabActive] =
      ImVec4(0.302f, 0.361f, 0.455f, 1.00f);  // #4D5C74

  // === Plot Lines (Used for sliders/curves) ===
  colors[ImGuiCol_PlotLines] =
      ImVec4(0.302f, 0.361f, 0.455f, 1.00f);  // #4D5C74

  // === Slider ===
  colors[ImGuiCol_SliderGrab] =
      ImVec4(0.302f, 0.361f, 0.455f, 1.00f);  // #4D5C74
  colors[ImGuiCol_SliderGrabActive] = ImVec4(
      0.369f, 0.439f, 0.545f, 1.00f);  // #5E708B (Slightly brighter on click)

  // === Nav ===
  colors[ImGuiCol_NavWindowingHighlight] =
      ImVec4(0.302f, 0.361f, 0.455f, 1.00f);  // #4D5C74
  colors[ImGuiCol_NavHighlight] =
      ImVec4(0.302f, 0.361f, 0.455f, 1.00f);  // #4D5C74

  // === Grip ===
  colors[ImGuiCol_ResizeGrip] = ImVec4(0.129f, 0.129f, 0.129f, 0.00f);
  colors[ImGuiCol_ResizeGripHovered] =
      ImVec4(0.302f, 0.361f, 0.455f, 1.00f);  // #4D5C74
  colors[ImGuiCol_ResizeGripActive] =
      ImVec4(0.302f, 0.361f, 0.455f, 1.00f);  // #4D5C74

  // === Docking ===
  colors[ImGuiCol_DockingPreview] =
      ImVec4(0.302f, 0.361f, 0.455f, 0.40f);  // #4D5C74 with alpha
  colors[ImGuiCol_DockingEmptyBg] =
      ImVec4(0.059f, 0.059f, 0.059f, 1.00f);  // #0F0F0F

  // === Style Tuning ===
  style.FrameBorderSize = 0.0f;
  style.WindowBorderSize = 1.0f;  // Thin border separating panels
  style.PopupBorderSize = 1.0f;
  style.TabBorderSize = 0.0f;
  style.ChildBorderSize = 0.0f;

  style.WindowRounding = 0.0f;
  style.FrameRounding = 3.0f;
  style.ScrollbarRounding = 3.0f;
  style.TabRounding = 2.0f;
  style.GrabRounding = 3.0f;
  style.PopupRounding = 4.0f;

  style.WindowPadding =
      ImVec2(EditorSizes::window_padding, EditorSizes::window_padding);
  style.FramePadding =
      ImVec2(EditorSizes::frame_padding, EditorSizes::frame_padding);
  style.ItemSpacing = ImVec2(4.0f, 4.0f);
  style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);

  style.GrabMinSize = 12.0f;
}

void Initialize() {
  ImGuiIO& io = ImGui::GetIO();
  LoadFonts(io);
  SetupStyle();

#ifdef IMGUI_HAS_DOCK
  ImGui::GetStyle().WindowMenuButtonPosition = ImGuiDir_None;

  ImGuiStyle& style = ImGui::GetStyle();
  style.TabBarOverlineSize =
      2.0f;  // Replicates Godot's top-blue accent on selected tabs
#endif
}

}  // namespace EditorStyles
