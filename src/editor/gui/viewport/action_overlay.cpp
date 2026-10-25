#include "action_overlay.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "editor/gui/styles/editor_styles.h"
#include "editor/gui/viewport/viewport_overlay.h"

namespace Viewport {

namespace {

std::string g_text;
std::string g_key;          // empty when there's no keycap
double g_posted_at = -1.0;  // ImGui time of the last post

constexpr float kHold = 1.5f;    // seconds fully visible
constexpr float kFade = 0.5f;    // seconds to fade out
constexpr float kKeyGap = 6.0f;  // keycap to text
const ImVec2 kMargin =
    ImVec2(EditorSizes::overlay_margin, EditorSizes::overlay_margin);
const ImVec2 kPadding = ImVec2(10.0f, 6.0f);

// Multiply a color's alpha by factor (0..1)
ImU32 ScaleAlpha(ImU32 color, float factor) {
  const float a =
      static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) * factor;
  return (color & ~IM_COL32_A_MASK) |
         (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
}

}  // namespace

void PostAction(const std::string& text, const char* key) {
  g_text = text;
  g_key = key ? key : "";
  g_posted_at = ImGui::GetTime();
}

void ActionOverlay(ImVec2 canvas_min, ImVec2 canvas_size) {
  if (g_posted_at < 0.0 || g_text.empty())
    return;

  const float elapsed = static_cast<float>(ImGui::GetTime() - g_posted_at);
  if (elapsed >= kHold + kFade)
    return;
  const float alpha =
      elapsed <= kHold ? 1.0f : 1.0f - (elapsed - kHold) / kFade;

  ImFont* font = EditorStyles::GetFonts().s;
  const float font_size = font->FontSize;
  const ImVec2 text_size =
      font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, g_text.c_str());

  // Keycap: at least square, wider for multi-letter keys
  const float cap_h = std::floor(font_size + 4.0f);
  float cap_w = 0.0f;
  ImVec2 key_size = ImVec2(0.0f, 0.0f);
  if (!g_key.empty()) {
    key_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, g_key.c_str());
    cap_w = std::max(cap_h, std::floor(key_size.x + 8.0f));
  }

  const float content_w = (cap_w > 0.0f ? cap_w + kKeyGap : 0.0f) + text_size.x;
  const float content_h = std::max(cap_h, text_size.y);
  const ImVec2 panel_size = ImVec2(std::floor(content_w + kPadding.x * 2.0f),
                                   std::floor(content_h + kPadding.y * 2.0f));

  const ImVec2 p0 = AnchorToCorner(Corner::BottomLeft, canvas_min, canvas_size,
                                   panel_size, kMargin);
  const ImVec2 p1 = ImVec2(p0.x + panel_size.x, p0.y + panel_size.y);
  const float mid_y = p0.y + panel_size.y * 0.5f;

  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  draw_list->AddRectFilled(p0, p1, ScaleAlpha(EditorColor::overlay_bg, alpha),
                           EditorSizes::overlay_radius);

  float x = p0.x + kPadding.x;
  if (cap_w > 0.0f) {
    const ImVec2 c0 = ImVec2(x, std::floor(mid_y - cap_h * 0.5f));
    draw_list->AddRectFilled(c0, ImVec2(c0.x + cap_w, c0.y + cap_h),
                             ScaleAlpha(EditorColor::surface_4, alpha),
                             EditorSizes::control_radius);
    draw_list->AddText(font, font_size,
                       ImVec2(std::floor(c0.x + (cap_w - key_size.x) * 0.5f),
                              std::floor(c0.y + (cap_h - key_size.y) * 0.5f)),
                       ScaleAlpha(EditorColor::text_bright, alpha),
                       g_key.c_str());
    x += cap_w + kKeyGap;
  }

  draw_list->AddText(font, font_size,
                     ImVec2(x, std::floor(mid_y - text_size.y * 0.5f)),
                     ScaleAlpha(EditorColor::text, alpha), g_text.c_str());
}

}  // namespace Viewport