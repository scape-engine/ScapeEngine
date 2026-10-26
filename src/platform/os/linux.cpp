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

void StyleTitleBar(SDL_Window*, float, float, float) {
  return false;
}

}  // namespace Platform
