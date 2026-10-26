#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>

#include "platform/platform_utils.h"

#include <SDL_syswm.h>
#include <backends/imgui_impl_sdl2.h>

// Attribute ids from Windows 11 SDK
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif

namespace Platform {

namespace {

// 0..1 float channel to 0..255
BYTE ToByte(float value) {
  value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
  return static_cast<BYTE>(value * 255.0f + 0.5f);
}

}  // namespace

float GetContentScale(SDL_Window* window) {
  // Only meaningful once the process is DPI-aware
  float dpi = 96.0f;
  const int display = SDL_GetWindowDisplayIndex(window);
  if (display >= 0 && SDL_GetDisplayDPI(display, &dpi, nullptr, nullptr) == 0)
    return dpi / 96.0f;  // 96 DPI = 100%
  return 1.0f;
}

void EnableHighDpi() {
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");  // SDL 2.24+
}

void SetupBGFXPlatformData(bgfx::Init& init, SDL_Window* window) {
  int dw, dh;
  GetDrawableSize(window, &dw, &dh);
  init.resolution.width = dw;
  init.resolution.height = dh;
  init.resolution.reset = BGFX_RESET_VSYNC;

  SDL_SysWMinfo wmi;
  SDL_VERSION(&wmi.version);
  if (SDL_GetWindowWMInfo(window, &wmi)) {
    init.platformData.ndt = nullptr;
    init.platformData.nwh = wmi.info.win.window;
    init.platformData.context = nullptr;
  }
}

void InitSDLForImGui(SDL_Window* window) {
  ImGui_ImplSDL2_InitForOther(window);
}

bool StyleTitleBar(SDL_Window* window, float r, float g, float b) {
  SDL_SysWMinfo wmi;
  SDL_VERSION(&wmi.version);
  if (!SDL_GetWindowWMInfo(window, &wmi))
    return false;

  HWND hwnd = wmi.info.win.window;

  const BOOL dark = TRUE;
  HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark,
                                     sizeof(dark));
  if (FAILED(hr))
    hr = DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));

  const COLORREF color = RGB(ToByte(r), ToByte(g), ToByte(b));
  const bool exact = SUCCEEDED(
      DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &color, sizeof(color)));
  DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &color, sizeof(color));

  SendMessage(hwnd, WM_NCACTIVATE, FALSE, 0);
  SendMessage(hwnd, WM_NCACTIVATE, TRUE, 0);

  return exact;
}

}  // namespace Platform
