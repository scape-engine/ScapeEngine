#include "platform_utils.h"

#include <imgui.h>

namespace Platform {

// Pixel size of the drawable area, for any graphics backend (SDL 2.26+)
void GetDrawableSize(SDL_Window* window, int* out_w, int* out_h) {
  SDL_GetWindowSizeInPixels(window, out_w, out_h);
}

float GetFramebufferScale(SDL_Window* window) {
  int w = 0, h = 0, pixel_w = 0, pixel_h = 0;
  SDL_GetWindowSize(window, &w, &h);
  GetDrawableSize(window, &pixel_w, &pixel_h);
  return w > 0 ? static_cast<float>(pixel_w) / static_cast<float>(w) : 1.0f;
}

float GetDPIScale(SDL_Window* window) {
  return GetFramebufferScale(window);
}

void RefreshFramebufferSize(SDL_Window* win) {
  int dw = 0, dh = 0;
  GetDrawableSize(win, &dw, &dh);
  if (dw == 0 || dh == 0)  // window is minimised or off-screen
    return;                // skip reset

  bgfx::reset(dw, dh, BGFX_RESET_VSYNC);

  const float scale = GetFramebufferScale(win);
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(dw / scale, dh / scale);
  io.DisplayFramebufferScale = ImVec2(scale, scale);
}

void ToggleBorderlessFullscreen(SDL_Window* w, bool enable) {
  if (enable)
    RestoreMinSize(w);  // let Cocoa expand
  SDL_SetWindowFullscreen(w, enable ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);

  if (!enable)
    LockMinSize(w, 1280, 720);  // restore window clamp
  RefreshFramebufferSize(w);
}

namespace {
float g_title_bar_bottom = 0.0f;
std::vector<WindowRect> g_title_bar_interactive;
}  // namespace

void SetTitleBarRegion(float bottom,
                       const std::vector<WindowRect>& interactive) {
  g_title_bar_bottom = bottom;
  g_title_bar_interactive = interactive;
}

bool InTitleBarDragArea(float x, float y) {
  if (y < 0.0f || y >= g_title_bar_bottom)
    return false;
  for (const WindowRect& r : g_title_bar_interactive)
    if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
      return false;
  return true;
}

void MinimizeWindow(SDL_Window* window) {
  SDL_MinimizeWindow(window);
}

bool IsWindowMaximized(SDL_Window* window) {
  return SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED;
}

void ToggleMaximizeWindow(SDL_Window* window) {
  if (IsWindowMaximized(window))
    SDL_RestoreWindow(window);
  else
    SDL_MaximizeWindow(window);
}

void RequestWindowClose() {
  SDL_Event quit{};
  quit.type = SDL_QUIT;
  SDL_PushEvent(&quit);
}

bool IsBorderlessFullscreen(SDL_Window* win) {
  return SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
}

bool InFullscreenSpace(SDL_Window* w) {
  return SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN_DESKTOP;
}

void LockMinSize(SDL_Window* w, int minW, int minH) {
  SDL_SetWindowMinimumSize(w, minW, minH);
  SDL_SetWindowMaximumSize(w, 0, 0);  // 0 = can enlarge
}

void RestoreMinSize(SDL_Window* w) {
  SDL_SetWindowMinimumSize(w, 0, 0);  // remove lower bound
}

void DisableTextInput() {
  SDL_StopTextInput();
}

void EnableTextInput() {
  SDL_StartTextInput();
}

bool IsTextInputActive() {
  return SDL_IsTextInputActive() == SDL_TRUE;
}

// TODO: move to input/cursor
glm::vec2 GetCursorPosition() {
  int x, y;
  SDL_GetMouseState(&x, &y);
  return glm::vec2(static_cast<float>(x), static_cast<float>(y));
}

void SetCursorPosition(SDL_Window* window, const glm::vec2& pos) {
  SDL_WarpMouseInWindow(window, static_cast<int>(pos.x),
                        static_cast<int>(pos.y));
}

glm::vec2 GetGlobalCursorPosition() {
  int x, y;
  SDL_GetGlobalMouseState(&x, &y);
  return glm::vec2(static_cast<float>(x), static_cast<float>(y));
}

void SetGlobalCursorPosition(const glm::vec2& pos) {
  SDL_WarpMouseGlobal(static_cast<int>(pos.x), static_cast<int>(pos.y));
}

}  // namespace Platform
