#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
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

WNDPROC g_sdl_wndproc = nullptr;

// Resize border thickness at the window's DPI
int FrameThickness(HWND hwnd) {
  const UINT dpi = GetDpiForWindow(hwnd);
  return GetSystemMetricsForDpi(SM_CYFRAME, dpi) +
         GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

// Wraps SDL's window procedure: removes the native caption but keeps the
// resize borders, and reports the app's title row as caption so the OS
// handles drag, double-click maximize, snap and the system menu
LRESULT CALLBACK CustomTitleBarProc(HWND hwnd, UINT msg, WPARAM wparam,
                                    LPARAM lparam) {
  switch (msg) {
    case WM_NCCALCSIZE: {
      if (!wparam)
        break;
      // Default sizing keeps the side and bottom borders; undo the top one
      auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
      const LONG top = params->rgrc[0].top;
      const LRESULT result =
          CallWindowProc(g_sdl_wndproc, hwnd, msg, wparam, lparam);
      params->rgrc[0].top = top;
      // Maximized windows overhang the monitor by the frame thickness
      if (IsZoomed(hwnd))
        params->rgrc[0].top += FrameThickness(hwnd);
      return result;
    }

    case WM_GETMINMAXINFO: {
      // SDL converts the minimum client size to a window size assuming a
      // native top frame. Without that frame the client comes out taller
      // than requested, and since the editor sets the minimum to the
      // current size on every resize, the window grows forever
      const LRESULT result =
          CallWindowProc(g_sdl_wndproc, hwnd, msg, wparam, lparam);
      RECT frame = {0, 0, 0, 0};
      AdjustWindowRectExForDpi(
          &frame, static_cast<DWORD>(GetWindowLongPtr(hwnd, GWL_STYLE)), FALSE,
          static_cast<DWORD>(GetWindowLongPtr(hwnd, GWL_EXSTYLE)),
          GetDpiForWindow(hwnd));
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      info->ptMinTrackSize.y -= -frame.top;  // the top frame we removed
      return result;
    }

    case WM_NCHITTEST: {
      const LRESULT hit =
          CallWindowProc(g_sdl_wndproc, hwnd, msg, wparam, lparam);
      if (hit != HTCLIENT)
        return hit;  // side and bottom resize borders

      POINT p = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ScreenToClient(hwnd, &p);
      if (!IsZoomed(hwnd) && p.y < FrameThickness(hwnd))
        return HTTOP;  // the top border we removed in WM_NCCALCSIZE
      if (InTitleBarDragArea(static_cast<float>(p.x), static_cast<float>(p.y)))
        return HTCAPTION;
      return HTCLIENT;
    }
  }
  return CallWindowProc(g_sdl_wndproc, hwnd, msg, wparam, lparam);
}

}  // namespace

bool DrawsWindowButtons() {
  return true;
}

void EnableCustomTitleBar(SDL_Window* window) {
  SDL_SysWMinfo wmi;
  SDL_VERSION(&wmi.version);
  if (g_sdl_wndproc || !SDL_GetWindowWMInfo(window, &wmi))
    return;

  HWND hwnd = wmi.info.win.window;
  g_sdl_wndproc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(
      hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(CustomTitleBarProc)));
  // Re-run WM_NCCALCSIZE so the caption goes away now
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
               SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                   SWP_NOACTIVATE);
}

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
