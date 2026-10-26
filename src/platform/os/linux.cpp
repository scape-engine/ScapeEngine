#include "platform/platform_utils.h"

#include <SDL_syswm.h>
#include <backends/imgui_impl_sdl2.h>

namespace Platform {

float GetContentScale(SDL_Window*) {
  return 1.0f;  // TODO: read the desktop's scale (e.g. Xft.dpi / 96)
}

void EnableHighDpi() {}

void SetupBGFXPlatformData(bgfx::Init& init, SDL_Window* window) {
  int dw, dh;
  GetDrawableSize(window, &dw, &dh);
  init.resolution.width = dw;
  init.resolution.height = dh;
  init.resolution.reset = BGFX_RESET_VSYNC;

  SDL_SysWMinfo wmi;
  SDL_VERSION(&wmi.version);
  if (SDL_GetWindowWMInfo(window, &wmi)) {
    init.platformData.ndt = wmi.info.x11.display;
    init.platformData.nwh = (void*)(uintptr_t)wmi.info.x11.window;
    init.platformData.context = nullptr;
  }
}

void InitSDLForImGui(SDL_Window* window) {
  ImGui_ImplSDL2_InitForOther(window);
}

bool StyleTitleBar(SDL_Window*, float, float, float) {
  return false;
}

bool DrawsWindowButtons() {
  return true;
}

// Borderless window; SDL asks the app what each point is. No double-click
// maximize or snap: those belong to the window manager's own decorations
void EnableCustomTitleBar(SDL_Window* window) {
  SDL_SetWindowBordered(window, SDL_FALSE);
  SDL_SetWindowHitTest(
      window,
      [](SDL_Window* win, const SDL_Point* p, void*) -> SDL_HitTestResult {
        const int border = 6;  // resize grab width
        int w, h;
        SDL_GetWindowSize(win, &w, &h);
        if (!(SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED)) {
          const bool left = p->x < border, right = p->x >= w - border;
          const bool top = p->y < border, bottom = p->y >= h - border;
          if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
          if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
          if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
          if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
          if (top) return SDL_HITTEST_RESIZE_TOP;
          if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
          if (left) return SDL_HITTEST_RESIZE_LEFT;
          if (right) return SDL_HITTEST_RESIZE_RIGHT;
        }
        return InTitleBarDragArea(static_cast<float>(p->x),
                                  static_cast<float>(p->y))
                   ? SDL_HITTEST_DRAGGABLE
                   : SDL_HITTEST_NORMAL;
      },
      nullptr);
}

}  // namespace Platform
