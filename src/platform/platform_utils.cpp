#include "platform_utils.h"

#include <SDL.h>
#include <SDL_syswm.h>
#include <backends/imgui_impl_sdl2.h>

#if defined(_WIN32) || defined(__linux__)

namespace Platform {

void GetDrawableSize(SDL_Window* window, int* out_w, int* out_h) {
  // use SDL_Renderer
  if (SDL_Renderer* r = SDL_GetRenderer(window)) {
    SDL_GetRendererOutputSize(r, out_w, out_h);
  } else  // fall back to GL / D3D drawable size.
  {
    SDL_GL_GetDrawableSize(window, out_w, out_h);
  }
}

float GetDPIScale(SDL_Window* window) {
  int logicalW, logicalH;
  SDL_GetWindowSize(window, &logicalW, &logicalH);
  int drawableW, drawableH;
  SDL_GL_GetDrawableSize(window, &drawableW, &drawableH);
  return logicalW > 0 ? static_cast<float>(drawableW) / logicalW : 1.0f;
}

void* CreateMetalLayer(void* native_window) {
  // Not applicable on non-macOS
  return nullptr;
}

void SetupBGFXPlatformData(bgfx::Init& init, SDL_Window* window) {
  int w, h;
  SDL_GetWindowSize(window, &w, &h);
  init.resolution.width = w;
  init.resolution.height = h;
  init.resolution.reset = BGFX_RESET_VSYNC;

  SDL_SysWMinfo wmi;
  SDL_VERSION(&wmi.version);
  if (SDL_GetWindowWMInfo(window, &wmi)) {
#if defined(_WIN32)
    init.platformData.ndt = nullptr;
    init.platformData.nwh = wmi.info.win.window;
    init.platformData.context = nullptr;
#elif defined(__linux__)
    init.platformData.ndt = wmi.info.x11.display;
    init.platformData.nwh = (void*)(uintptr_t)wmi.info.x11.window;
    init.platformData.context = nullptr;
#endif
  }
}

void InitSDLForImGui(SDL_Window* window) {
  ImGui_ImplSDL2_InitForOther(window);
}

void ToggleBorderlessFullscreen(SDL_Window* w, bool enable) {
  if (enable)
    RestoreMinSize(w);  // let Cocoa expand
  SDL_SetWindowFullscreen(w, enable ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);

  if (!enable)
    LockMinSize(w, 1280, 720);  // restore window clamp
  RefreshFramebufferSize(w);
}

bool IsBorderlessFullscreen(SDL_Window* win) {
  return SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
}

void RefreshFramebufferSize(SDL_Window* win) {
  int dw, dh;
  SDL_GL_GetDrawableSize(win, &dw, &dh);
  bgfx::reset(dw, dh, BGFX_RESET_VSYNC);
  ImGuiIO& io = ImGui::GetIO();
  float dpi = GetDPIScale(win);
  io.DisplaySize = ImVec2(dw / dpi, dh / dpi);
  io.DisplayFramebufferScale = ImVec2(dpi, dpi);

  if (dw == 0 || dh == 0)  // window is minimised or off-screen
    return;                // skip reset
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

void StyleTitleBar(SDL_Window* window, float r, float g, float b) {
  // Implementation for styling the title bar
}

// TODO: move to /input/cursor
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

#endif
